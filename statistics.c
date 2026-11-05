/*==================================================================
  statistics.c  ——  票房统计 + 排行
  ------------------------------------------------------------------
  口径说明（答辩可以直接讲这一段）：
  票房不再读 Schedule.income，而是直接数订单流水（ticket.dat）。
      state == 1  有效票      → 计入收入
      state == 2  已退票      → 不计（钱已经退了）
      state == 3  已改签老票  → 不计（钱转到新票上了，新票是 state==1）
  好处：管理员清理过期场次、删掉排片记录时，历史票房一分不少。
  排片表只是"现在放什么"的索引，订单流水才是账本。
==================================================================*/
#include "cinema.h"

static int movieNameOf(int movieId, char* out)   /* 影片名写入 out，找不到写"未知影片" */
{
    int i;
    for (i = 0; i < g_movieCnt; i++)
        if (g_movies[i].id == movieId)
        {
            strcpy(out, g_movies[i].name); return i;
        }
    strcpy(out, "未知影片");
    return -1;
}

/*------------------------------------------------------------------
  按场次把订单流水归拢一遍
    inc[i] / cnt[i]  第 i 个场次的有效票收入、有效票张数
    archCnt/archIncome  场次已经被清理掉、只剩订单的那些票
  返回：全部有效票的收入合计（= 历史总票房）
------------------------------------------------------------------*/
static float sumTicketsBySchedule(float* inc, int* cnt,
                                  int* archCnt, float* archIncome)
{
    int   i, si;
    float total = 0.0f;

    for (i = 0; i < g_schedCnt; i++) { inc[i] = 0.0f; cnt[i] = 0; }
    *archCnt    = 0;
    *archIncome = 0.0f;

    for (i = 0; i < g_ticketCnt; i++)
    {
        if (g_tickets[i].state != 1 &&
            g_tickets[i].state != 4) continue;      /* 只算有效票：2 已退、3 改签老票都不算钱 */

        total += g_tickets[i].price;

        si = findSchedule(g_tickets[i].schedId);
        if (si >= 0)                    /* 场次还在 */
        {
            inc[si] += g_tickets[i].price;
            cnt[si] += 1;
        }
        else                            /* 场次已被清理，但订单还在 */
        {
            *archCnt   += 1;
            *archIncome += g_tickets[i].price;
        }
    }
    return total;
}

void calculateBoxOffice(void)
{
    float inc[MAX_SCHEDULES];
    int   cnt[MAX_SCHEDULES];
    int   archCnt = 0, i, sold = 0;
    float archIncome = 0.0f, total;
    char  mname[50];

    system("cls");
    printf("================ 票房统计 ================\n");

    total = sumTicketsBySchedule(inc, cnt, &archCnt, &archIncome);

    if (g_schedCnt == 0)
    {
        printf("  当前没有排片。\n");
    }
    else
    {
        printf("  %-10s  %-5s  %-6s  %-9s  %9s  影片\n",
            "日期", "时段", "影厅", "售出/总座", "收入");
        printf("  ------------------------------------------------------------\n");

        for (i = 0; i < g_schedCnt; i++)
        {
            movieNameOf(g_scheds[i].movieId, mname);
            printf("  %-10s  %-5s  %d号厅    %3d/%-3d   %8.2f  %s\n",
                g_scheds[i].date, slotTime(g_scheds[i].slot),
                g_scheds[i].hallId, cnt[i], SEAT_TOTAL, inc[i], mname);

            sold += cnt[i];
        }
    }

    printf("  ------------------------------------------------------------\n");
    printf("  现存 %d 场   有效票 %d 张   收入 %.2f 元\n",
        g_schedCnt, sold, total - archIncome);

    if (g_schedCnt > 0)
        printf("  平均上座率 %.1f%%（只算现存场次）\n",
            sold * 100.0 / (g_schedCnt * (double)SEAT_TOTAL));

    if (archCnt > 0)
        printf("  已清理场次的历史订单 %d 张   %.2f 元（已计入总票房）\n",
            archCnt, archIncome);

    printf("  ============================================================\n");
    printf("  历史总票房 %.2f 元   历史总售票 %d 张\n",
        total, sold + archCnt);
    pauseScreen();
}

void sortBoxOffice(void)
{
    int   idx[MAX_SCHEDULES];
    float inc[MAX_SCHEDULES];
    int   cnt[MAX_SCHEDULES];
    int   archCnt = 0, n, i, j, tmp, s;
    float archIncome = 0.0f, total;
    char  mname[50];

    system("cls");
    printf("================ 票房排行 ================\n");

    total = sumTicketsBySchedule(inc, cnt, &archCnt, &archIncome);
    n = g_schedCnt;

    if (n == 0)
    {
        printf("  当前没有排片。\n");
    }
    else
    {
        for (i = 0; i < n; i++) idx[i] = i;     /* 只排下标，不动原数组 */

        for (i = 0; i < n - 1; i++)             /* 冒泡，从高到低 */
            for (j = 0; j < n - 1 - i; j++)
                if (inc[idx[j]] < inc[idx[j + 1]])
                {
                    tmp = idx[j];
                    idx[j] = idx[j + 1];
                    idx[j + 1] = tmp;
                }

        for (i = 0; i < n; i++)
        {
            s = idx[i];
            movieNameOf(g_scheds[s].movieId, mname);
            printf("  第%2d名  %8.2f元  《%s》  %s %s %d号厅  %d张\n",
                i + 1, inc[s], mname, g_scheds[s].date,
                slotTime(g_scheds[s].slot), g_scheds[s].hallId, cnt[s]);
        }
    }

    printf("  ------------------------------------------------------------\n");
    if (archCnt > 0)
        printf("  另有已清理场次的订单 %d 张  %.2f 元\n", archCnt, archIncome);
    printf("  历史总票房 %.2f 元\n", total);
    pauseScreen();
}
