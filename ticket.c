/*==================================================================
  ticket.c —— 购票 / 退票 / 我的订单
  对外只有三个函数，别的一概不写
==================================================================*/
#include "cinema.h"
#include <string.h>

/*------------------------------------------------------------------
  退改手续费
    left     = 距开演还有多少分钟（minutesToStart 的返回值）
    isChange = 0 退票，1 改签
  ------------------------------------------------------------------
  分档照线上票务平台的做法：越临近开演越贵，因为座位来不及再卖。
      24 小时以上      免费
      6 ~ 24 小时      2 元
      2 ~ 6 小时       5 元
      2 小时以内       10 元
  改签一律是退票的一半 —— 用户人还在、只是换个场次，影院没丢这笔钱。
  但改签票不能再退（见 refundTicket），否则"先改签到远处再免费退"
  就能把阶梯整个绕过去。
  已开场的由调用方拦住，这里不重复判断。
------------------------------------------------------------------*/
float calcFee(long left, int isChange)
{
    float fee;

    if (left > 24 * 60)      fee = 0.0f;
    else if (left > 6 * 60)  fee = 2.0f;
    else if (left > 2 * 60)  fee = 5.0f;
    else                     fee = 10.0f;

    /* 半价后四舍五入到元：0->0, 2->1, 5->3, 10->5 */
    if (isChange)
        fee = (float)(int)(fee * 0.5f + 0.5f);

    return fee;
}

/*------------------------------------------------------------------
  1. 购票
  成功返回 1，取消或失败返回 0
------------------------------------------------------------------*/
int buyTicket(int uid)
{
    int sIdx, r, c, i, left, used = 0;
    char mname[50] = "";
    Ticket* t;

    /* ① 订单表满了就别再卖了，数组会越界 */
    if (g_ticketCnt >= MAX_TICKETS) {
        printf("\n  订单表已满（上限 %d 条），暂时无法出票。\n", MAX_TICKETS);
        pauseScreen();
        return 0;
    }

    /* ② 选场次，返回数组下标，-1 表示放弃 */
    sIdx = pickSchedule();
    if (sIdx < 0) {
        printf("\n  已取消购票。\n");
        pauseScreen();
        return 0;
    }

    /* ③ 限购：数这场里这人还有几张有效票 */
    for (i = 0; i < g_ticketCnt; i++) {
        if (g_tickets[i].userId == uid &&
            g_tickets[i].schedId == g_scheds[sIdx].id &&
            (g_tickets[i].state == 1 || g_tickets[i].state == 4)) {
            used++;
        }
    }
    if (used >= MAX_PER_USER) {
        printf("\n  同一场次每人最多买 %d 张，你已经买了 %d 张。\n",
            MAX_PER_USER, used);
        pauseScreen();
        return 0;
    }

    /* ④ 看座位图，顺便判断有没有空位 */
    showSeats(sIdx);
    left = countLeft(sIdx);
    if (left <= 0) {
        printf("\n  这一场已经满座了。\n");
        pauseScreen();
        return 0;
    }
    printf("\n  剩余空座：%d 个，请挑一个。\n", left);

    /* ⑤ 输入排号座号，-1 放弃 */
    printf("  排号 1 ~ %d（图上 A ~ J，输入 -1 放弃购票）：", ROW_COUNT);
    r = inputInt(-1, ROW_COUNT);
    if (r < 0) {
        printf("\n  已取消购票。\n");
        pauseScreen();
        return 0;
    }

    printf("  座号 1 ~ %d（图上从左往右数，输入 -1 放弃购票）：", COL_COUNT);
    c = inputInt(-1, COL_COUNT);
    if (c < 0) {
        printf("\n  已取消购票。\n");
        pauseScreen();
        return 0;
    }

    r--;  c--;          /* 用户从 1 数起，数组从 0 数起 */

    /* ⑥ 占座：seatSell 负责改 seat 数组和 soldCnt，返回 0 代表被人抢先了 */
    if (!seatSell(sIdx, r, c)) {
        printf("\n  这个座位刚刚被别人买走了，请重新选。\n");
        pauseScreen();
        return 0;
    }

    /* ⑦ 取片名做快照，管理员以后改片名调价格都不影响这张老票 */
    for (i = 0; i < g_movieCnt; i++) {
        if (g_movies[i].id == g_scheds[sIdx].movieId) {
            strcpy(mname, g_movies[i].name);
            break;
        }
    }

    /* ⑧ 写订单 */
    t = &g_tickets[g_ticketCnt];
    t->id = newTicketId();
    t->userId = uid;
    t->schedId = g_scheds[sIdx].id;
    strcpy(t->movieName, mname);
    strcpy(t->date, g_scheds[sIdx].date);
    t->slot = g_scheds[sIdx].slot;
    t->row = r;
    t->col = c;
    t->price = g_scheds[sIdx].price;
    t->hallId = g_scheds[sIdx].hallId;      /* 影厅号也存一份快照 */
    getNowTime(t->buyTime);                 /* 下单时间，票面上要显示 */

    /* 购票人也存快照：以后用户在"账号设置"里改了昵称，
       这张票上仍然是下单当时那个名字，账目才对得上 */
    strncpy(t->buyerName, userNameById(uid), 19);
    t->buyerName[19] = '\0';

    t->state = 1;
    g_ticketCnt++;

    /* ⑨ 票房在这里加，seatSell 里不管钱 */
    g_scheds[sIdx].income += g_scheds[sIdx].price;

    saveCinema();
    saveTickets();

    /* ⑩ 小票 */
    printf("\n  ---------------- 购 票 成 功 ----------------\n");
    printf("   票号：%d\n", t->id);
    printf("   影片：《%s》\n", t->movieName);
    printf("   场次：%s  %s\n", t->date, slotTime(t->slot));
    printf("   影厅：%d 号厅\n", t->hallId);
    printf("   座位：%d 排 %d 座\n", r + 1, c + 1);
    printf("   金额：%.2f 元\n", t->price);
    printf("   购票人：%s\n", t->buyerName);
    printf("   购票：%s\n", t->buyTime);
    printf("  --------------------------------------------\n");
    pauseScreen();
    return 1;
}

/*------------------------------------------------------------------
  档位文字：把 left 翻成"免费/扣2元/..."，列表的说明列用
  和 calcFee 共用同一组边界，改档位只改这两处
------------------------------------------------------------------*/
static const char* feeTag(long left)
{
    if (left > 24 * 60) return "免费";
    if (left > 6 * 60)  return "扣2元";
    if (left > 2 * 60)  return "扣5元";
    return "扣10元";
}

/*------------------------------------------------------------------
  2. 退票
  成功返回 1，取消或失败返回 0
------------------------------------------------------------------*/
int refundTicket(int uid)
{
    int   i, tid, real = -1, sIdx, n = 0, pass, started;
    long  left = 0;
    float fee = 0.0f, back = 0.0f;
    char  when[20], seatBuf[16], nameBuf[20];
    const char* cFee;
    const char* cEnd = g_colorOn ? "\x1b[0m" : "";
    Ticket* t;

    /* 列表里每张票当时算出的手续费，确认时拿来比：跨档了要明说。
       static 是因为 2500 个 float 放栈上太大；每次调用先清成 -1。 */
    static float feeShown[MAX_TICKETS];

    /* ① 列有效票：能退的排前面，退不了的沉底。
          手续费不存盘，每次重画都按"现在"重算 —— 同一张票隔一天再打开，
          数字会自己往上涨，这正是阶梯收费要的效果。
          列宽：2+4+2+16+2+11+2+8+2+6+2+6+2+7+2+6 = 78 列，80 列控制台不折行 */
    for (i = 0; i < MAX_TICKETS; i++) feeShown[i] = -1.0f;

    printf("\n============== 可退票的订单 ==============\n");
    printf("  %-4s  %-16s  %-11s  %-8s  %6s  %6s  %7s  %-6s\n",
        "票号", "影片", "场次", "座位", "票价", "手续费", "实退", "说明");

    for (pass = 0; pass < 2; pass++)          /* 0 = 能退的，1 = 不能退的 */
    {
        for (i = 0; i < g_ticketCnt; i++)
        {
            if (g_tickets[i].userId != uid) continue;
            if (g_tickets[i].state != 1 && g_tickets[i].state != 4) continue;

            sIdx    = findSchedule(g_tickets[i].schedId);
            started = isStarted(sIdx);
            if (pass == 0 && (g_tickets[i].state == 4 || started)) continue;
            if (pass == 1 && g_tickets[i].state != 4 && !started) continue;
            n++;

            /* 日期只取后 5 位 MM-DD，和时段拼成一列，省 4 列宽 */
            sprintf(when, "%s %s", g_tickets[i].date + 5,
                slotTime(g_tickets[i].slot));
            sprintf(seatBuf, "%d排%2d座", g_tickets[i].row + 1, g_tickets[i].col + 1);
            strncpy(nameBuf, g_tickets[i].movieName, 16);   /* 8 个汉字 */
            nameBuf[16] = '\0';

            if (g_tickets[i].state == 4 || started)
            {
                /* 退不了的票也列出来，但要标明原因，不然用户以为程序丢了票 */
                printf("  %4d  %-16s  %-11s  %-8s  %6.2f  %6s  %7s  %s%s%s\n",
                    g_tickets[i].id, nameBuf, when, seatBuf, g_tickets[i].price,
                    "--", "--",
                    g_colorOn ? "\x1b[1;31m" : "",
                    g_tickets[i].state == 4 ? "改签票" : "已开场", cEnd);
            }
            else
            {
                left = minutesToStart(sIdx);
                fee  = calcFee(left, 0);
                back = g_tickets[i].price - fee;
                feeShown[i] = fee;
                cFee = g_colorOn ? (fee <= 0.0f ? "\x1b[1;32m" : "\x1b[1;33m") : "";

                printf("  %4d  %-16s  %-11s  %-8s  %6.2f  %6.2f  %7.2f  %s%s%s\n",
                    g_tickets[i].id, nameBuf, when, seatBuf, g_tickets[i].price,
                    fee, back, cFee, feeTag(left), cEnd);
            }
        }
    }

    if (n == 0)
    {
        printf("  你还没有可退的票。\n");
        pauseScreen();
        return 0;
    }

    /* ② 直接输票号：和"我的订单"里看到的是同一个号，不会错位 */
    printf("\n  请输入要退的票号（0 放弃）：");
    tid = inputInt(0, 1000000);
    if (tid == 0)
    {
        printf("\n  已取消退票。\n");
        pauseScreen();
        return 0;
    }

    /* 按票号找下标：先只认"是不是自己的票"，状态单独判断，
       这样才能区分"没这张票"和"这张票不能退" */
    for (i = 0; i < g_ticketCnt; i++)
    {
        if (g_tickets[i].id == tid && g_tickets[i].userId == uid)
        { real = i; break; }
    }
    if (real < 0)
    {
        printf("\n  没有 %d 号的订单，请照着上面的票号输入。\n", tid);
        pauseScreen();
        return 0;
    }

    t = &g_tickets[real];

    /* 改签票不能再退。这是"每笔订单限改签一次"的另一半：
       否则可以先改签到很远的场次，再按免费档退掉，把阶梯绕过去 */
    if (t->state == 4)
    {
        printf("\n  这是改签票，按规则不能退，只能去看。\n");
        pauseScreen();
        return 0;
    }
    if (t->state != 1)
    {
        printf("\n  这张票已经退过或改签过了，不能重复退。\n");
        pauseScreen();
        return 0;
    }

    /* ③ 已经开场的场次不给退 —— 电影都放完了还退，等于白看一场。
          注意这里比的是"开场时间"不是"日期"：
          今天 10:00 的场，晚上 20:00 来退，日期还是今天，必须挡住 */
    sIdx = findSchedule(t->schedId);
    if (isStarted(sIdx))
    {
        printf("\n  这场已经开场了，票不能退。\n");
        pauseScreen();
        return 0;
    }

    /* ③-2 算手续费：越临近开演扣得越多，规则在 calcFee 里 */
    left = minutesToStart(sIdx);
    fee  = calcFee(left, 0);
    back = t->price - fee;

    /* 从"打印列表"到"按确认"之间可能刚好跨过档位（剩 6:00 时打开列表，
       填票号花了两分钟）。数字变了就明说，别让人以为程序前后不一致。 */
    if (feeShown[real] >= 0.0f && feeShown[real] != fee)
        printf("\n  提示：已跨过档位，手续费由 %.2f 元变为 %.2f 元。\n",
            feeShown[real], fee);

    printf("\n  ================ 退票确认 ================\n");
    printf("  《%s》  %s  %s  %d排%d座\n", t->movieName, t->date,
        slotTime(t->slot), t->row + 1, t->col + 1);
    printf("  距开演还有 %ld 小时 %ld 分\n", left / 60, left % 60);
    printf("  票面 %.2f 元，手续费 %.2f 元（%s），实退 %.2f 元\n",
        t->price, fee, feeTag(left), back);

    printf("\n  确认退票？（1=确认，0=取消）：");
    if (inputInt(0, 1) == 0)
    {
        printf("\n  已取消退票。\n");
        pauseScreen();
        return 0;
    }

    /* ④ 把座位放回去，票房减掉 */
    if (sIdx >= 0)
    {
        seatFree(sIdx, t->row, t->col);
        if (g_scheds[sIdx].income >= t->price)
            g_scheds[sIdx].income -= t->price;
        else
            g_scheds[sIdx].income = 0.0;     /* 兜底，不许出现负票房 */
    }

    /* ④ 票不删，只改状态，留痕 */
    t->state = 2;

    saveCinema();
    saveTickets();

    printf("\n  退票成功：%d 号票已作废，手续费 %.2f 元，实退 %.2f 元。\n",
        t->id, fee, back);
    pauseScreen();
    return 1;
}

/*------------------------------------------------------------------
  3. 我的订单（含已退的，方便对账）
------------------------------------------------------------------*/
void showMyTickets(int uid)
{
    int    i, n = 0, valid = 0;
    double paid = 0.0;
    const Ticket* t;
    char   hall[16];

    printf("\n==================== 我的订单 ====================\n");
    printf("  %-4s  %-20s  %-10s  %-5s  %-5s  %-8s  %6s  %s\n",
        "票号", "影片", "日期", "时段", "影厅", "座位", "票价", "状态");

    /* 倒序：最近买的排在最上面 */
    for (i = g_ticketCnt - 1; i >= 0; i--)
    {
        t = &g_tickets[i];
        if (t->userId != uid) continue;
        n++;

        /* 影厅用票上的快照，不去查场次 —— 场次可能已经被清理了 */
        if (t->hallId >= 1 && t->hallId <= HALL_COUNT)
            sprintf(hall, "%d号厅", t->hallId);
        else
            strcpy(hall, "已下架");

        printf("  %4d  %-20s  %-10s  %-5s  %-5s  %2d排%2d座  %6.2f  %s\n",
            t->id, t->movieName, t->date, slotTime(t->slot), hall,
            t->row + 1, t->col + 1, t->price,
            t->state == 1 ? "有效"
            : (t->state == 4 ? "有效(改签票)"
            : (t->state == 3 ? "已改签" : "已退")));

        if (t->buyTime[0] != '\0')
            printf("        购票时间：%s", t->buyTime);
        else
            printf("        购票时间：（旧订单，没有记录）");

        /* 购票人是下单时的快照：用户改过名，这里仍显示当时那个名字 */
        if (t->buyerName[0] != '\0')
            printf("    购票人：%s\n", t->buyerName);
        else
            printf("    购票人：（旧订单，未记录）\n");

        if (t->state == 1 || t->state == 4)     /* 改签票也是有效票，能进场 */
        {
            valid++;
            paid += t->price;
        }
    }

    if (n == 0)
    {
        printf("  你还没有任何订单。\n");
    }
    else
    {
        printf("  -------------------------------------------------------------------------\n");
        printf("  共 %d 条订单，其中有效 %d 张，合计 %.2f 元。\n", n, valid, paid);
    }
    pauseScreen();
}

/*------------------------------------------------------------------
  4. 改签（换场次）
  成功返回 1，取消或失败返回 0

  它和退票的根本区别：
    退票是"票没了，钱退你"；改签是"票还在，只是换一场"。
    所以改签不能走退票再买票的老路 —— 那样会多出一条已退记录，
    用户看到两张票，账面上也乱。

  这里的做法是：老票标成"已改签"留痕，另开一张新票指向新场次。
  三处数据要同时改，任何一处失败都得退回去，不能只改一半。
------------------------------------------------------------------*/
int changeTicket(int uid)
{
    int   n = 0, i, tid, real, oldIdx, newIdx;
    int   row, col, mIdx;
    long  left;
    float oldPrice, newPrice, diff, fee, pay;
    Ticket* t;
    Ticket  nt;
    const char* mname;

    system("cls");
    printf("=================== 改签 ===================\n");

    /* ① 订单表得留一格：改签是"作废旧票 + 开一张新票"，
          不是原地改记录，所以必须有空位 */
    if (g_ticketCnt >= MAX_TICKETS)
    {
        printf("  订单表已满（上限 %d 条），暂时无法改签。\n", MAX_TICKETS);
        pauseScreen();
        return 0;
    }

    /* ② 只列有效票：已退的、已经改签过的都不能再改 */
    printf("\n  可以改签的票：\n");
    printf("  %-4s  %-20s  %-10s  %-5s  %-8s  %6s\n",
        "票号", "影片", "日期", "时段", "座位", "票价");

    for (i = 0; i < g_ticketCnt; i++)
    {
        if (g_tickets[i].userId != uid || g_tickets[i].state != 1) continue;

        n++;
        printf("  %4d  %-20s  %-10s  %-5s  %2d排%2d座  %6.2f\n",
            g_tickets[i].id, g_tickets[i].movieName, g_tickets[i].date,
            slotTime(g_tickets[i].slot),
            g_tickets[i].row + 1, g_tickets[i].col + 1, g_tickets[i].price);
    }

    if (n == 0)
    {
        printf("  你还没有可以改签的票。\n");
        pauseScreen();
        return 0;
    }

    printf("\n  请输入要改签的票号（0 取消）：");
    tid = inputInt(0, 1000000);
    if (tid == 0)
    {
        printf("\n  已取消。\n");
        pauseScreen();
        return 0;
    }

    real = -1;
    for (i = 0; i < g_ticketCnt; i++)
    {
        if (g_tickets[i].id == tid && g_tickets[i].userId == uid &&
            g_tickets[i].state == 1) { real = i; break; }
    }
    if (real < 0)
    {
        printf("\n  没有 %d 号的可改签订单，请照着上面的票号输入。\n", tid);
        pauseScreen();
        return 0;
    }

    t = &g_tickets[real];

    /* ③ 原场次还在不在、过没过期
          票是好的，但那天已经过去了，改签没有意义 */
    oldIdx = findSchedule(t->schedId);
    if (oldIdx < 0)
    {
        printf("\n  原场次已经不存在了，这张票只能退，不能改签。\n");
        pauseScreen();
        return 0;
    }
    if (isStarted(oldIdx))
    {
        printf("\n  原场次已经开场了，不能改签。\n");
        pauseScreen();
        return 0;
    }

    printf("\n  原票：《%s》  %s  %s  %d排%d座  %.2f 元\n",
        t->movieName, t->date, slotTime(t->slot),
        t->row + 1, t->col + 1, t->price);

    /* ④ 选新场次：pickSchedule 内部已经校验过"没过期、没售完" */
    printf("\n  改签到哪一场？\n");
    newIdx = pickSchedule();
    if (newIdx < 0)
    {
        printf("\n  已取消。\n");
        pauseScreen();
        return 0;
    }
    if (newIdx == oldIdx)
    {
        printf("\n  还是原来那一场，不用改。\n");
        pauseScreen();
        return 0;
    }

    /* ⑤ 看新场次的座位图，重新选座 */
    showSeats(newIdx);

    printf("\n  请输入新排号（1~%d，0 取消）：", ROW_COUNT);
    row = inputInt(0, ROW_COUNT);
    if (row == 0)
    {
        printf("\n  已取消。\n");
        pauseScreen();
        return 0;
    }
    row--;

    printf("  请输入新座位号（1~%d，0 取消）：", COL_COUNT);
    col = inputInt(0, COL_COUNT);
    if (col == 0)
    {
        printf("\n  已取消。\n");
        pauseScreen();
        return 0;
    }
    col--;

    /* 到这里才换算成 0 基下标，前面 0 一直是"取消"的意思 */
    if (g_scheds[newIdx].seat[row][col] != 0)
    {
        printf("\n  %s排%d座已经卖出去了，换一个吧。\n",
            rowName(row), col + 1);
        pauseScreen();
        return 0;
    }

    /* ⑥ 算差价 + 手续费：改签不是免费换，贵的补、便宜的退，
          另外收一笔改签手续费（退票费的一半，按距开演的时间分档） */
    left     = minutesToStart(oldIdx);
    fee      = calcFee(left, 1);
    oldPrice = t->price;
    newPrice = g_scheds[newIdx].price;
    diff     = newPrice - oldPrice;
    pay      = diff + fee;              /* >0 要付钱，<0 退钱 */

    printf("\n  ================ 改签确认 ================\n");
    printf("  原：%s  %-6s  %d排%d座  %6.2f 元\n",
        t->date, slotTime(t->slot), t->row + 1, t->col + 1, oldPrice);
    printf("  新：%s  %-6s  %d排%d座  %6.2f 元\n",
        g_scheds[newIdx].date, slotTime(g_scheds[newIdx].slot),
        row + 1, col + 1, newPrice);

    printf("  距原场次开演还有 %ld 小时 %ld 分\n", left / 60, left % 60);

    if (diff > 0.005f)
        printf("  补差价 %.2f 元\n", diff);
    else if (diff < -0.005f)
        printf("  退差价 %.2f 元\n", -diff);
    else
        printf("  票价相同，不用补差价\n");
    printf("  改签手续费 %.2f 元（退票费的一半）\n", fee);

    if (pay > 0.005f)
        printf("  合计需要支付 %.2f 元\n", pay);
    else if (pay < -0.005f)
        printf("  合计退还 %.2f 元\n", -pay);
    else
        printf("  合计不补不退\n");

    printf("\n  确认改签？（1=确认，0=取消）：");
    if (inputInt(0, 1) == 0)
    {
        printf("\n  已取消。\n");
        pauseScreen();
        return 0;
    }

    /* ⑦ 检查全部通过，这里才开始动数据。
          顺序：先腾出旧座，再占新座 —— 反过来会短时间内占两个座 */

    seatFree(oldIdx, t->row, t->col);
    if (g_scheds[oldIdx].income >= oldPrice)
        g_scheds[oldIdx].income -= oldPrice;
    else
        g_scheds[oldIdx].income = 0.0f;      /* 兜底，不许出现负票房 */

    /* 占新座。正常走不到失败分支（前面刚查过空位），
       但真失败了必须把旧座还回去，不能变成"票没了座位也没了" */
    if (!seatSell(newIdx, row, col))
    {
        seatSell(oldIdx, t->row, t->col);
        g_scheds[oldIdx].income += oldPrice;
        printf("\n  改签失败：新座位刚被占了，原票已还原。\n");
        pauseScreen();
        return 0;
    }
    g_scheds[newIdx].income += newPrice;

    /* 老票不删、也不是"已退"，单独标成 3 = 已改签，留个痕 */
    t->state = 3;

    /* 新票：完全按购票的格式生成一条，字段一个不少 */
    mIdx  = findMovie(g_scheds[newIdx].movieId);
    mname = (mIdx >= 0) ? g_movies[mIdx].name : "未知电影";

    memset(&nt, 0, sizeof(nt));
    nt.id     = newTicketId();
    nt.userId = uid;
    nt.schedId = g_scheds[newIdx].id;
    strncpy(nt.movieName, mname, 49);
    nt.movieName[49] = '\0';
    strcpy(nt.date, g_scheds[newIdx].date);
    nt.slot  = g_scheds[newIdx].slot;
    nt.row   = row;
    nt.col   = col;
    nt.price = newPrice;
    nt.hallId = g_scheds[newIdx].hallId;
    getNowTime(nt.buyTime);

    /* 购票人跟着老票走：改签只是换场次，不换人；
       万一老票是没记名字的旧数据，才补当前的用户名 */
    if (t->buyerName[0] != '\0')
        strncpy(nt.buyerName, t->buyerName, 19);
    else
        strncpy(nt.buyerName, userNameById(uid), 19);
    nt.buyerName[19] = '\0';
    nt.state = 4;            /* 4 = 改签来的票：有效、能进场，但不能再退改 */

    g_tickets[g_ticketCnt] = nt;
    g_ticketCnt++;

    saveCinema();
    saveTickets();

    printf("\n  改签成功！新票号 %d。\n", nt.id);
    printf("  差价 %.2f 元，手续费 %.2f 元", diff, fee);
    if (pay > 0.005f)
        printf("，合计支付 %.2f 元。\n", pay);
    else if (pay < -0.005f)
        printf("，合计退还 %.2f 元。\n", -pay);
    else
        printf("，不用补退。\n");
    printf("  注意：改签后的票不能再退，也不能再改签。\n");

    pauseScreen();
    return 1;
}
