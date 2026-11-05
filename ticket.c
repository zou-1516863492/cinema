/*==================================================================
  ticket.c —— 购票 / 退票 / 我的订单
  对外只有三个函数，别的一概不写
==================================================================*/
#include "cinema.h"
#include <string.h>

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
            g_tickets[i].state == 1) {
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
    printf("  排号 1 ~ %d（输入 -1 放弃购票）：", ROW_COUNT);
    r = inputInt(-1, ROW_COUNT);
    if (r < 0) {
        printf("\n  已取消购票。\n");
        pauseScreen();
        return 0;
    }

    printf("  座号 1 ~ %d（输入 -1 放弃购票）：", COL_COUNT);
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
    printf("   购票：%s\n", t->buyTime);
    printf("  --------------------------------------------\n");
    pauseScreen();
    return 1;
}

/*------------------------------------------------------------------
  2. 退票
  成功返回 1，取消或失败返回 0
------------------------------------------------------------------*/
int refundTicket(int uid)
{
    int  i, tid, real = -1, sIdx, n = 0;
    char hall[16];
    Ticket* t;

    /* ① 列有效票，前面的数字就是票号本身，不再重数序号 */
    printf("\n============== 可退票的订单 ==============\n");
    printf("  %-4s  %-20s  %-10s  %-5s  %-5s  %-8s  %6s\n",
        "票号", "影片", "日期", "时段", "影厅", "座位", "票价");

    for (i = 0; i < g_ticketCnt; i++)
    {
        if (g_tickets[i].userId != uid || g_tickets[i].state != 1) continue;
        n++;

        if (g_tickets[i].hallId >= 1 && g_tickets[i].hallId <= HALL_COUNT)
            sprintf(hall, "%d号厅", g_tickets[i].hallId);
        else
            strcpy(hall, "已下架");

        printf("  %4d  %-20s  %-10s  %-5s  %-5s  %2d排%2d座  %6.2f\n",
            g_tickets[i].id, g_tickets[i].movieName, g_tickets[i].date,
            slotTime(g_tickets[i].slot), hall,
            g_tickets[i].row + 1, g_tickets[i].col + 1, g_tickets[i].price);
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

    /* 按票号找下标：票号全局唯一，但还要确认是自己的、并且仍然有效 */
    for (i = 0; i < g_ticketCnt; i++)
    {
        if (g_tickets[i].id == tid && g_tickets[i].userId == uid &&
            g_tickets[i].state == 1) { real = i; break; }
    }
    if (real < 0)
    {
        printf("\n  没有 %d 号的可退订单，请照着上面的票号输入。\n", tid);
        pauseScreen();
        return 0;
    }

    t = &g_tickets[real];

    /* ③ 反查场次，把座位放回去，票房减掉 */
    sIdx = findSchedule(t->schedId);
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

    printf("\n  退票成功：%d 号票已作废，退款 %.2f 元。\n", t->id, t->price);
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
            : (t->state == 3 ? "已改签" : "已退"));

        if (t->buyTime[0] != '\0')
            printf("        购票时间：%s\n", t->buyTime);
        else
            printf("        购票时间：（旧订单，没有记录）\n");

        if (t->state == 1)
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
    float oldPrice, newPrice, diff;
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
    if (strcmp(g_scheds[oldIdx].date, g_today) < 0)
    {
        printf("\n  原场次已经过期，不能改签。\n");
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

    /* ⑥ 算差价：改签不是免费换，贵的补、便宜的退 */
    oldPrice = t->price;
    newPrice = g_scheds[newIdx].price;
    diff     = newPrice - oldPrice;

    printf("\n  ================ 改签确认 ================\n");
    printf("  原：%s  %-6s  %d排%d座  %6.2f 元\n",
        t->date, slotTime(t->slot), t->row + 1, t->col + 1, oldPrice);
    printf("  新：%s  %-6s  %d排%d座  %6.2f 元\n",
        g_scheds[newIdx].date, slotTime(g_scheds[newIdx].slot),
        row + 1, col + 1, newPrice);

    if (diff > 0.005f)
        printf("  需要补差价 %.2f 元\n", diff);
    else if (diff < -0.005f)
        printf("  将退还差价 %.2f 元\n", -diff);
    else
        printf("  票价相同，不用补差价\n");

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
    nt.state = 1;

    g_tickets[g_ticketCnt] = nt;
    g_ticketCnt++;

    saveCinema();
    saveTickets();

    printf("\n  改签成功！新票号 %d。\n", nt.id);
    if (diff > 0.005f)
        printf("  请补差价 %.2f 元。\n", diff);
    else if (diff < -0.005f)
        printf("  已退还差价 %.2f 元。\n", -diff);

    pauseScreen();
    return 1;
}
