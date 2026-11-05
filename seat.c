/*==================================================================
  seat.c  ——  座位图 + 座位状态
  规则：seat[r][c] == 0 空位，== 1 已售
        只有 seatSell / seatFree 能改这个值，别处一律只读
==================================================================*/
#include "cinema.h"

/* 一个座位格：方块字符 + 1 个空格。
   方块本身占几列由 g_cellW 决定（启动时实测），所以格子总宽 = g_cellW + 1，
   表头用同样的宽度格式，数字就一定落在方块正上方，换字体也不会错。 */
static void seatCell(int sold)
{
    int i, pad = 3 - g_cellW;              /* 方块自身 g_cellW 列，补足到 3 列 */

    if (pad < 1) pad = 1;
    if (g_colorOn) printf(sold ? "\x1b[1;31m" : "\x1b[1;32m");
    printf("%s", sold ? "■" : "□");
    if (g_colorOn) printf("\x1b[0m");
    for (i = 0; i < pad; i++) putchar(' ');
}

void showSeats(int sIdx)
{
    char mname[50];
    int  r, c, k, i, sold, left, fill, pct;
    const char* cTitle = g_colorOn ? "\x1b[1;36m" : "";
    const char* cFilm  = g_colorOn ? "\x1b[1;33m" : "";
    const char* cRow   = g_colorOn ? "\x1b[36m"    : "";
    const char* cFill  = g_colorOn ? "\x1b[1;32m"  : "";
    const char* cBlank = g_colorOn ? "\x1b[90m"    : "";
    const char* cEnd   = g_colorOn ? "\x1b[0m"     : "";

    if (sIdx < 0 || sIdx >= g_schedCnt)
    {
        printf("\n  场次不存在。\n");
        return;
    }

    strcpy(mname, "未知影片");
    for (k = 0; k < g_movieCnt; k++)
        if (g_movies[k].id == g_scheds[sIdx].movieId)
        {
            strcpy(mname, g_movies[k].name); break;
        }

    left = countLeft(sIdx);
    sold = SEAT_TOTAL - left;
    pct  = sold * 100 / SEAT_TOTAL;
    fill = sold * 20 / SEAT_TOTAL;
    if (sold > 0 && fill == 0) fill = 1;          /* 有票就至少亮一格 */

    printf("\n  %s=============  银  幕  =============%s\n\n", cTitle, cEnd);
    printf("  %s《%s》%s  %s  %s（第%d场）  %d号厅  票价 %.2f 元\n",
        cFilm, mname, cEnd, g_scheds[sIdx].date, slotTime(g_scheds[sIdx].slot),
        g_scheds[sIdx].slot, g_scheds[sIdx].hallId, g_scheds[sIdx].price);

    printf("      ");                                     /* 6 列，与排号前缀等宽 */
    for (c = 1; c <= COL_COUNT; c++) printf("%-2d ", c);   /* 每格固定 3 列 */
    printf("\n");

    for (r = 0; r < ROW_COUNT; r++)
    {
        printf("  %s%s%s   ", cRow, rowName(r), cEnd);      /* 2+1+3 = 6 列 */
        for (c = 0; c < COL_COUNT; c++)
            seatCell(g_scheds[sIdx].seat[r][c] != 0);
        printf("\n");
    }

    printf("\n  ");
    seatCell(0); printf(" 空位      ");
    seatCell(1); printf(" 已售\n");

    printf("  上座 [");
    for (i = 0; i < 20; i++)
        printf("%s%c%s", i < fill ? cFill : cBlank, i < fill ? '#' : '-', cEnd);
    printf("]  %d%%   余票 %d / %d\n", pct, left, SEAT_TOTAL);
}

/* 数还剩多少个空座 */
int countLeft(int sIdx)
{
    int r, c, n = 0;

    if (sIdx < 0 || sIdx >= g_schedCnt) return 0;

    for (r = 0; r < ROW_COUNT; r++)
        for (c = 0; c < COL_COUNT; c++)
            if (g_scheds[sIdx].seat[r][c] == 0) n++;

    return n;
}

/* 占座：成功 1，位置非法或已售 0。不存盘、不动票房，由调用方负责 */
int seatSell(int sIdx, int row, int col)
{
    if (sIdx < 0 || sIdx >= g_schedCnt) return 0;
    if (row < 0 || row >= ROW_COUNT)    return 0;
    if (col < 0 || col >= COL_COUNT)    return 0;
    if (g_scheds[sIdx].seat[row][col] != 0) return 0;   /* 已经卖出去了 */

    g_scheds[sIdx].seat[row][col] = 1;
    g_scheds[sIdx].soldCnt++;
    return 1;
}

/* 退座：成功 1，位置非法或本来就空着 0 */
int seatFree(int sIdx, int row, int col)
{
    if (sIdx < 0 || sIdx >= g_schedCnt) return 0;
    if (row < 0 || row >= ROW_COUNT)    return 0;
    if (col < 0 || col >= COL_COUNT)    return 0;
    if (g_scheds[sIdx].seat[row][col] != 1) return 0;   /* 本来就没卖出去 */

    g_scheds[sIdx].seat[row][col] = 0;
    if (g_scheds[sIdx].soldCnt > 0) g_scheds[sIdx].soldCnt--;
    return 1;
}
