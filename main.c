/*==================================================================
  main.c  ——  程序入口 + 数据层 + 公共工具
  ------------------------------------------------------------------
  负责人：A
  包含四块：
    1) 全局数据实体（头文件里只 extern，实体在这里定义一次）
    2) 数据层  dataInit / loadXxx / saveXxx
    3) 工具    inputInt / inputStr / pauseScreen / slotTime / rowName / addDays / newXxxId
    4) main    主菜单循环
==================================================================*/
#include "cinema.h"
#include <direct.h>     /* _mkdir */

/*==================== 1. 全局数据实体 ====================*/
/* 头文件里是 extern，实体只能在这里定义一次。
   别的 .c 里再写一遍 User g_users[...] 就会 LNK2005 重复定义。 */
User     g_users[MAX_USERS];
int      g_userCnt = 0;

Movie    g_movies[MAX_MOVIES];
int      g_movieCnt = 0;

Schedule g_scheds[MAX_SCHEDULES];
int      g_schedCnt = 0;

Ticket   g_tickets[MAX_TICKETS];
int      g_ticketCnt = 0;

char     g_today[11] = { 0 };

/*==================== 2. 数据层 ====================*/
/* 读写规则（双方必须一致，改了要通知对方）：
     user.dat   : [int 条数][User × 条数]
     cinema.dat : [int 影片条数][Movie × 条数][int 场次条数][Schedule × 条数]
     ticket.dat : [int 条数][Ticket × 条数]
   一律 "rb"/"wb" 二进制模式。用 "r"/"w" 会因为换行符转换让整个结构体错位。 */

   /* 取系统当天日期，写进 out（10 字符 + '\0'） */
static void getToday(char* out)
{
    time_t    t = time(NULL);
    struct tm* p = localtime(&t);
    sprintf(out, "%04d-%02d-%02d", p->tm_year + 1900, p->tm_mon + 1, p->tm_mday);
}

/*------------------------------------------------------------------
  取当前时刻，写成 "YYYY-MM-DD HH:MM:SS"（19 字符）
  和 getToday 的区别：那个只要日期，排片用；这个要精确到秒，下单用
  buf 至少要 20 字节
------------------------------------------------------------------*/
void getNowTime(char* out)
{
    time_t    t = time(NULL);
    struct tm* p = localtime(&t);
    sprintf(out, "%04d-%02d-%02d %02d:%02d:%02d",
        p->tm_year + 1900, p->tm_mon + 1, p->tm_mday,
        p->tm_hour, p->tm_min, p->tm_sec);
}

void loadUsers(void)
{
    FILE* fp;
    g_userCnt = 0;
    fp = fopen(FILE_USER, "rb");
    if (fp == NULL) return;                 /* 首次运行正常，无文件即无数据 */

    if (fread(&g_userCnt, sizeof(int), 1, fp) != 1)
    {
        g_userCnt = 0;
        fclose(fp);
        return;
    }
    if (g_userCnt < 0 || g_userCnt > MAX_USERS)   /* 文件损坏，整份丢弃 */
    {
        printf("警告：user.dat 条数异常(%d)，已重置。\n", g_userCnt);
        g_userCnt = 0;
        fclose(fp);
        return;
    }
    if (g_userCnt > 0)
        fread(g_users, sizeof(User), g_userCnt, fp);
    fclose(fp);
}

void saveUsers(void)
{
    FILE* fp = fopen(FILE_USER, "wb");
    if (fp == NULL)
    {
        printf("user.dat 保存失败，请检查 write 权限。\n");
        return;
    }
    fwrite(&g_userCnt, sizeof(int), 1, fp);
    if (g_userCnt > 0)
        fwrite(g_users, sizeof(User), g_userCnt, fp);
    fclose(fp);
}

void loadCinema(void)
{
    FILE* fp;
    g_movieCnt = 0;
    g_schedCnt = 0;
    fp = fopen(FILE_CINEMA, "rb");
    if (fp == NULL) return;

    /* 第一段：影片 */
    if (fread(&g_movieCnt, sizeof(int), 1, fp) != 1) { g_movieCnt = 0; fclose(fp); return; }
    if (g_movieCnt < 0 || g_movieCnt > MAX_MOVIES)
    {
        printf("警告：cinema.dat 影片条数异常(%d)，已重置。\n", g_movieCnt);
        g_movieCnt = g_schedCnt = 0;
        fclose(fp);
        return;
    }
    if (g_movieCnt > 0)
        fread(g_movies, sizeof(Movie), g_movieCnt, fp);

    /* 第二段：场次（座位在 Schedule 里，一起读进来） */
    if (fread(&g_schedCnt, sizeof(int), 1, fp) != 1) { g_schedCnt = 0; fclose(fp); return; }
    if (g_schedCnt < 0 || g_schedCnt > MAX_SCHEDULES)
    {
        printf("警告：cinema.dat 场次条数异常(%d)，已重置场次。\n", g_schedCnt);
        g_schedCnt = 0;
        fclose(fp);
        return;
    }
    if (g_schedCnt > 0)
        fread(g_scheds, sizeof(Schedule), g_schedCnt, fp);
    fclose(fp);
}

void saveCinema(void)
{
    FILE* fp = fopen(FILE_CINEMA, "wb");
    if (fp == NULL)
    {
        printf("cinema.dat 保存失败，请检查 write 权限。\n");
        return;
    }
    fwrite(&g_movieCnt, sizeof(int), 1, fp);
    if (g_movieCnt > 0)
        fwrite(g_movies, sizeof(Movie), g_movieCnt, fp);

    fwrite(&g_schedCnt, sizeof(int), 1, fp);
    if (g_schedCnt > 0)
        fwrite(g_scheds, sizeof(Schedule), g_schedCnt, fp);
    fclose(fp);
}

typedef struct
{
    int   id;
    int   userId;
    int   schedId;
    char  movieName[50];
    char  date[11];
    int   slot;
    int   row;
    int   col;
    float price;
    int   state;
} TicketV1;                 /* 旧版订单：96 字节/条，没有影厅号和下单时间 */

void loadTickets(void)
{
    FILE*    fp;
    TicketV1 old;
    int      n = 0, i, si;
    long     fileSize;

    g_ticketCnt = 0;
    fp = fopen(FILE_TICKET, "rb");
    if (fp == NULL) return;

    if (fread(&n, sizeof(int), 1, fp) != 1) { fclose(fp); return; }
    if (n < 0 || n > MAX_TICKETS)
    {
        printf("警告：ticket.dat 条数异常(%d)，已重置。\n", n);
        fclose(fp);
        return;
    }

    /* 用文件实际大小判断是哪个版本：条数乘以单条长度，正好等于文件长度就对上了。
       这样旧数据不用手工删，程序自己升级一次。 */
    fseek(fp, 0, SEEK_END);
    fileSize = ftell(fp);
    fseek(fp, (long)sizeof(int), SEEK_SET);

    if (fileSize == (long)sizeof(int) + (long)n * (long)sizeof(Ticket))
    {
        /* 新格式，直接读 */
        if (n > 0)
            g_ticketCnt = (int)fread(g_tickets, sizeof(Ticket), (size_t)n, fp);
    }
    else if (fileSize == (long)sizeof(int) + (long)n * (long)sizeof(TicketV1))
    {
        /* 旧格式：逐条搬进新结构。影厅号反查场次补上，下单时间留空 */
        for (i = 0; i < n; i++)
        {
            if (fread(&old, sizeof(TicketV1), 1, fp) != 1) break;

            memset(&g_tickets[i], 0, sizeof(Ticket));
            g_tickets[i].id     = old.id;
            g_tickets[i].userId = old.userId;
            g_tickets[i].schedId = old.schedId;
            strncpy(g_tickets[i].movieName, old.movieName, 49);
            g_tickets[i].movieName[49] = '\0';
            strncpy(g_tickets[i].date, old.date, 10);
            g_tickets[i].date[10] = '\0';
            g_tickets[i].slot   = old.slot;
            g_tickets[i].row    = old.row;
            g_tickets[i].col    = old.col;
            g_tickets[i].price  = old.price;
            g_tickets[i].state  = old.state;
            g_tickets[i].buyTime[0] = '\0';      /* 旧票没有下单时间 */

            si = findSchedule(old.schedId);
            g_tickets[i].hallId = (si >= 0) ? g_scheds[si].hallId : 0;
        }
        g_ticketCnt = i;
        fclose(fp);

        printf("订单文件是旧格式，已升级：%d 条记录补上影厅号，下单时间留空。\n",
            g_ticketCnt);
        saveTickets();                    /* 立刻写成新格式，下次不用再迁 */
        return;
    }
    else
    {
        printf("警告：ticket.dat 大小异常（%ld 字节，%d 条），已重置。\n",
            fileSize, n);
        g_ticketCnt = 0;
    }

    fclose(fp);
}

void saveTickets(void)
{
    FILE* fp = fopen(FILE_TICKET, "wb");
    if (fp == NULL)
    {
        printf("ticket.dat 保存失败，请检查 write 权限。\n");
        return;
    }
    fwrite(&g_ticketCnt, sizeof(int), 1, fp);
    if (g_ticketCnt > 0)
        fwrite(g_tickets, sizeof(Ticket), g_ticketCnt, fp);
    fclose(fp);
}

void dataSaveAll(void)
{
    saveUsers();
    saveCinema();
    saveTickets();
}

void dataInit(void)
{
    _mkdir(DATA_DIR);           /* 已存在会返回 -1，不用管 */
    getToday(g_today);
    loadUsers();
    securePasswords();          /* 明文密码统一升级为哈希，只做一次 */
    loadCinema();
    loadTickets();

    printf("数据载入完成：用户 %d 个，影片 %d 部，场次 %d 场，订单 %d 条。\n",
        g_userCnt, g_movieCnt, g_schedCnt, g_ticketCnt);
    if (g_movieCnt == 0)
        printf("提示：还没有任何影片，请先用管理员账号排片。\n");
    pauseScreen();
}

/*==================== 3. 工具 ====================*/
/* 全项目禁止直接用 scanf：
     1) 输入字母会死循环刷屏；
     2) 会往缓冲区留下 '\n'，害得后面的 pauseScreen 被跳过。 */

int inputInt(int lo, int hi)
{
    char buf[64];
    int  v;

    while (1)
    {
        if (fgets(buf, sizeof(buf), stdin) == NULL)   /* Ctrl+Z 之类 */
            return lo;
        if (sscanf(buf, "%d", &v) == 1 && v >= lo && v <= hi)
            return v;
        printf("  输入无效，请输入 %d ~ %d 之间的整数：", lo, hi);
    }
}

void inputStr(char* buf, int size)
{
    char tmp[256];
    int  len;

    while (1)
    {
        if (fgets(tmp, sizeof(tmp), stdin) == NULL)
        {
            buf[0] = '\0';
            return;
        }
        tmp[strcspn(tmp, "\r\n")] = '\0';       /* 去掉行尾换行 */
        len = (int)strlen(tmp);
        if (len == 0)
        {
            printf("  不能为空，请重新输入：");
            continue;
        }
        if (len >= size)                        /* 注意：中文一个字占 3 字节 */
        {
            printf("  太长了（上限 %d 字节，约 %d 个汉字），请重新输入：",
                size - 1, (size - 1) / 2);   /* GBK 下一个汉字占 2 字节 */
            continue;
        }
        strcpy(buf, tmp);
        return;
    }
}

/*------------------------------------------------------------------
  密码输入：字符不回显，每按一个键打一个 *
  ─────────────────────────────────────────────────────────────
  为什么不能再用 inputStr：
    fgets 会把密码原样留在屏幕上，旁边的人一眼就看到了。
    这里改用 _getch() 逐字符读键盘，读到什么自己决定显示什么。

  支持的键：
    - 普通字符：存进缓冲区，屏幕上显示 *
    - 退格：从缓冲区删掉一个，屏幕上擦掉一个 *
    - 回车：结束
    - 方向键 / 功能键：一并吞掉（它们会返回两个字节，第二个不吞会变成乱码）

  一个细节：
    如果标准输入被重定向到文件（自动化测试就是这种情况），_getch() 读不到，
    所以先用 _isatty 判断一下：不是控制台就退回 getchar，按行读。
    人在键盘上敲的时候走 _getch，屏蔽回显；机器喂数据的时候走 getchar。
------------------------------------------------------------------*/
void inputPassword(char* buf, int size)
{
    int i = 0;
    int ch;
    int tty = _isatty(_fileno(stdin));      /* 非 0 表示 stdin 是控制台 */

    while (1)
    {
        ch = tty ? _getch() : getchar();

        if (ch == EOF)
        {
            break;
        }
        if (ch == '\r' || ch == '\n')       /* 回车：输入结束 */
        {
            break;
        }
        if (ch == 8 || ch == 127)           /* 退格：删掉最后一个字符 */
        {
            if (i > 0)
            {
                i--;
                printf("\b \b");            /* 退一格、用空格盖掉、再退回来 */
            }
            continue;
        }
        if (ch == 0 || ch == 224)           /* 方向键、F 键：后面还跟一个扫描码 */
        {
            _getch();
            continue;
        }
        if (i < size - 1)                   /* 到上限就不再收，屏幕上也不再出 * */
        {
            buf[i++] = (char)ch;
            putchar('*');
        }
    }

    buf[i] = '\0';
    printf("\n");
}

void pauseScreen(void)
{
    printf("\n按回车继续...");
    fflush(stdin);      /* MSVC 特有：清掉缓冲区残留的换行。换 GCC 要改成自己读掉 */
    getchar();
}

const char* slotTime(int slot)
{
    switch (slot)
    {
    case 1:  return "10:00";
    case 2:  return "14:30";
    case 3:  return "19:00";
    default: return "--:--";
    }
}

const char* rowName(int row)
{
    static const char* names[] = { "A", "B", "C", "D", "E",
                                   "F", "G", "H", "I", "J" };
    if (row < 0 || row >= (int)(sizeof(names) / sizeof(names[0])))
        return "?";
    return names[row];
}

void addDays(char* out, int days)
{
    time_t     t = time(NULL);
    struct tm* p = localtime(&t);

    p->tm_mday += days;
    mktime(p);                      /* 规范化，自动处理跨月跨年 */
    strftime(out, 11, "%Y-%m-%d", p);
}

/* 编号 = 现有最大编号 + 1。不回收，删掉的最多留个洞，避免 id 重复。 */
int newUserId(void)
{
    int i, max = 0;
    for (i = 0; i < g_userCnt; i++)
        if (g_users[i].id > max) max = g_users[i].id;
    return max + 1;
}

int newMovieId(void)
{
    int i, max = 0;
    for (i = 0; i < g_movieCnt; i++)
        if (g_movies[i].id > max) max = g_movies[i].id;
    return max + 1;
}

int newSchedId(void)
{
    int i, max = 0;
    for (i = 0; i < g_schedCnt; i++)
        if (g_scheds[i].id > max) max = g_scheds[i].id;
    return max + 1;
}

int newTicketId(void)
{
    int i, max = 0;
    for (i = 0; i < g_ticketCnt; i++)
        if (g_tickets[i].id > max) max = g_tickets[i].id;
    return max + 1;
}

/*==================== 4. 主菜单 ====================*/
void showMainMenu(void)
{
    printf("==============================================\n");
    printf("            模拟电影院售票系统\n");
    printf("            今天 %s   每场 %d 座\n", g_today, SEAT_TOTAL);
    printf("==============================================\n");
    printf("   1. 管理员登录\n");
    printf("   2. 用户注册\n");
    printf("   3. 用户登录\n");
    printf("   0. 退出系统\n");
    printf("----------------------------------------------\n");
    printf("请选择 [0-3]：");
}

int main(void)
{
    int  op;
    int  uid;
    char uname[20];

    dataInit();                     /* 建目录 + 取日期 + 三个文件读进内存 */

    while (1)
    {
        system("cls");
        showMainMenu();
        op = inputInt(0, 3);

        switch (op)
        {
        case 1:                                     /* 管理员 */
            if (adminLogin())
                adminMenu();                        /* 内部自带循环和暂停 */
            break;

        case 2:                                     /* 注册 */
            registerUser();                         /* 内部自带提示和暂停 */
            break;

        case 3:                                     /* 观众 */
            uname[0] = '\0';                        /* 清掉上一次登录残留的用户名 */
            uid = 0;
            if (loginUser(uname, &uid))
                userMenu(uid, uname);               /* 内部自带循环和暂停 */
            break;

        case 0:                                     /* 退出 */
            dataSaveAll();                          /* 平时"改完即存"，这里再兜一次底 */
            printf("\n数据已保存，程序退出。\n");
            return 0;
        }
    }
    return 0;
}
