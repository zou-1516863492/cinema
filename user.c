
#include "cinema.h"

/*==================================================================
  密码存储：不存明文，存"加盐哈希"
  ─────────────────────────────────────────────────────────────
  做法：把密码和用户编号混在一起，算 1024 轮 FNV-1a，得到一个
        64 位散列，再转成 16 个十六进制字符，前面加 '$' 标记
        "这一格存的是哈希，不是原密码"。
        最终 pwd 字段里长这样：  $a3f19c7e2b04d185

  为什么这么做：
    ① 不可逆  —— 从 $a3f1... 反推不出原密码，数据文件被人看到也没用
    ② 加盐    —— 混入了用户编号，两个人用同一个密码，存下来的值也
                 不同，别人没法靠"值相同"推断出"密码相同"
    ③ 迭代    —— 算 1024 轮，登录只多花几微秒，暴力枚举却慢 1024 倍

  为什么不用 MD5 / SHA：
    课设环境没有 OpenSSL，自己实现要一百多行。哈希"不可逆"靠的是单向
    函数这个性质，FNV-1a 虽然不是密码学哈希，但配上加盐和迭代，对本
    项目够用。真实系统应该用 bcrypt / scrypt / Argon2 这类专门为"存密码"
    设计的算法 —— 它们本身就是慢的，正是为了让暴力破解变慢。
==================================================================*/

/*------------------------------------------------------------------
  把密码 + 用户编号 揉成一个 64 位散列
------------------------------------------------------------------*/
static unsigned long long pwdHash(const char* pwd, int uid)
{
    unsigned long long h;
    const unsigned char* p;
    int round;

    /* 初始值掺入用户编号：这就是"盐"，
       换个用户，整条计算链的起点就不一样 */
    h = 14695981039346656037ULL ^ ((unsigned long long)uid * 2654435761ULL);

    /* 迭代 1024 轮：每轮都把密码重新揉一遍，
       想让破解慢 1024 倍，代价只是登录时多算几微秒 */
    for (round = 0; round < 1024; round++)
    {
        p = (const unsigned char*)pwd;
        while (*p)
        {
            h ^= (unsigned long long)(*p++);
            h *= 1099511628211ULL;              /* FNV-1a 的质数 */
        }
        h ^= (unsigned long long)round;         /* 让每轮的中间值都不同 */
    }
    return h;
}

/*------------------------------------------------------------------
  散列 → "$" + 16 位十六进制，写进 out（至少 20 字节）
------------------------------------------------------------------*/
static void hashPassword(const char* pwd, int uid, char* out)
{
    static const char* HEX = "0123456789abcdef";
    unsigned long long h = pwdHash(pwd, uid);
    int i;

    out[0] = '$';                               /* 标记：这格存的是哈希 */
    for (i = 0; i < 16; i++)
    {
        out[16 - i] = HEX[h & 0xF];
        h >>= 4;
    }
    out[17] = '\0';
}

/*------------------------------------------------------------------
  校验：把这次输入的密码用同样方式算一遍，和存下来的比
  返回：1 一致，0 不一致
------------------------------------------------------------------*/
static int checkPassword(const char* input, const char* stored, int uid)
{
    char tmp[20];

    /* 存量如果是旧格式（明文，没有 '$' 开头），说明迁移没跑到，
       这里做一次兜底：直接按明文比，避免用户彻底登不进去 */
    if (stored[0] != '$')
    {
        return strcmp(input, stored) == 0;
    }

    hashPassword(input, uid, tmp);
    return strcmp(tmp, stored) == 0;
}

/*------------------------------------------------------------------
  把历史遗留的明文密码，统一升级成哈希（启动时跑一次）
  判断依据：pwd 第一位是不是 '$'。是 → 已经是哈希，跳过。
  升级完立刻存盘，下次启动就没什么可做的了。
------------------------------------------------------------------*/
void securePasswords(void)
{
    int  i, cnt = 0;
    char tmp[20];

    for (i = 0; i < g_userCnt; i++)
    {
        if (g_users[i].pwd[0] == '$') continue;      /* 已经是哈希 */

        hashPassword(g_users[i].pwd, g_users[i].id, tmp);
        strcpy(g_users[i].pwd, tmp);
        cnt++;
    }

    if (cnt > 0)
    {
        saveUsers();
        printf("已把 %d 个账号的明文密码升级为哈希存储。\n", cnt);
    }
}

/*------------------------------------------------------------------
  密码强度校验：必须同时包含 字母 + 数字 + 符号
  ─────────────────────────────────────────────────────────────
  规则：
    ① 只能用英文字母、数字和半角符号（0x21 ~ 0x7E）
       空格、中文、全角符号一律不收 —— 它们在登录框里最容易输错
    ② 字母、数字、符号 三类各至少出现一次
    ③ 任何位置都不许出现 '$'
       '$' 是 pwd 字段里"这格存的是哈希"的标记，
       密码里带它会和标记撞车，导致这个账号永远登不进去

  参数：reason 用于回填失败原因（调用方要给够 64 字节）
  返回：1 合格，0 不合格
------------------------------------------------------------------*/
int checkPwdStrength(const char* pwd, char* reason)
{
    int i;
    int hasAlpha = 0, hasDigit = 0, hasSymbol = 0;

    for (i = 0; pwd[i] != '\0'; i++)
    {
        unsigned char c = (unsigned char)pwd[i];

        if (c == '$')
        {
            strcpy(reason, "密码里不能出现 $ 符号");
            return 0;
        }
        if (c < 0x21 || c > 0x7E)      /* 空格、中文、全角字符都进不来 */
        {
            strcpy(reason, "密码只能用英文字母、数字和半角符号");
            return 0;
        }

        if (c >= '0' && c <= '9')
            hasDigit = 1;                                  /* 数字 */
        else if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))
            hasAlpha = 1;                                  /* 字母 */
        else
            hasSymbol = 1;                                 /* 其余可见字符算符号 */
    }

    if (!hasAlpha || !hasDigit || !hasSymbol)
    {
        strcpy(reason, "密码必须同时包含 字母 + 数字 + 符号");
        return 0;
    }
    return 1;
}

/*-------------------- 1. 用户注册 --------------------*/
/* 返回：1 注册成功；0 已满 / 重名 / 两次密码不一致 */
int registerUser(void)
{
    char uname[20];
    char pwd1[20];
    char pwd2[20];
    char reason[64];
    int  i;

    system("cls");
    printf("=============== 用户注册 ===============\n");

    /* ① 容量检查：数组写满就没地方放了 */
    if (g_userCnt >= MAX_USERS)
    {
        printf("用户数已达上限（%d 个），暂时无法注册。\n", MAX_USERS);
        pauseScreen();
        return 0;
    }

    /* ② 用户名：inputStr 已保证非空且不超长 */
    printf("请输入用户名（最多 9 个汉字）：");
    inputStr(uname, sizeof(uname));

    /* ③ 查重：把已有账号全扫一遍 */
    for (i = 0; i < g_userCnt; i++)
    {
        if (strcmp(g_users[i].name, uname) == 0)
        {
            printf("\n该用户名已被注册，请换一个。\n");
            pauseScreen();
            return 0;
        }
    }

    /* ④ 密码输两遍，防手抖 */
    printf("请设置密码（字母+数字+符号，不能含 $）：");
    inputPassword(pwd1, sizeof(pwd1));
    printf("请再输一次密码：");
    inputPassword(pwd2, sizeof(pwd2));

    if (strcmp(pwd1, pwd2) != 0)
    {
        printf("\n两次输入的密码不一致，注册失败。\n");
        pauseScreen();
        return 0;
    }

    /* 密码强度：字母 + 数字 + 符号，三类各至少一个；
       具体是哪一类不满足，由 checkPwdStrength 写进 reason 里 */
    if (!checkPwdStrength(pwd1, reason))
    {
        printf("\n%s，注册失败。\n", reason);
        pauseScreen();
        return 0;
    }

    /* ⑤ 写入内存：id 由 newUserId() 算，永远不会重复 */
    g_users[g_userCnt].id = newUserId();
    strcpy(g_users[g_userCnt].name, uname);
    /* 密码不存明文：算成加盐哈希再存进去 */
    hashPassword(pwd1, g_users[g_userCnt].id, g_users[g_userCnt].pwd);
    g_userCnt++;

    saveUsers();

    printf("\n注册成功！你的用户编号是 %d，现在可以登录了。\n",
           g_users[g_userCnt - 1].id);
    pauseScreen();
    return 1;
}

/*-------------------- 2. 用户登录 --------------------*/
/* 返回：1 成功（回填 name 和 uid）；0 失败 */
int loginUser(char *name, int *uid)
{
    char uname[20];
    char pwd[20];
    int  i;

    /* 先清干净：main 里那个 uname 数组是复用的，
       上一次登录的名字还留在里面，不清会串号 */
    name[0] = '\0';
    *uid = 0;

    system("cls");
    printf("=============== 用户登录 ===============\n");

    printf("请输入用户名：");
    inputStr(uname, sizeof(uname));
    printf("请输入密码：");
    inputPassword(pwd, sizeof(pwd));

    for (i = 0; i < g_userCnt; i++)
    {
        if (strcmp(g_users[i].name, uname) != 0) continue;

        /* 用户名对上了，再用同样的算法把这次输入的密码算一遍，
           和存下来的哈希比。注意 uid 必须传进去 —— 它参与了加盐，
           传错值就会算出另一个哈希，正确密码也登不进 */
        if (checkPassword(pwd, g_users[i].pwd, g_users[i].id))
        {
            strncpy(name, g_users[i].name, 19);   /* 目标只有 20 字节，别溢出 */
            name[19] = '\0';
            *uid = g_users[i].id;

            printf("\n登录成功，欢迎回来，%s！\n", name);
            pauseScreen();
            return 1;
        }
        break;              /* 用户名唯一，名字对了密码不对就不用再找 */
    }

    printf("\n用户名或密码错误。\n");
    pauseScreen();
    return 0;
}

/*-------------------- 3. 账号设置 --------------------*/
/* 改昵称、改密码。
   name 传的是 main 里那个 uname 数组（20 字节），改名后要就地写回去，
   否则退回菜单还顶着旧名字。
   ─────────────────────────────────────────────────────────────
   关于"改名之后老票上是谁的名字"：
     订单里的 buyerName 是下单那一刻抄进去的快照，和影片名、影厅号一样，
     只认当时的值。这样改完名，历史订单照样能看出"这张票是谁买的"，
     账不会因为改名而错乱。新买的票才写新名字。
------------------------------------------------------------------*/
void userProfile(int uid, char *name)
{
    int  i, op, idx = -1;
    char buf[20], oldPwd[20], pwd1[20], pwd2[20], reason[64];

    /* 先定位账号在数组里的下标：登录进来的一定找得到，
       找不到说明数据被外部改过，直接退回去，不做任何写入 */
    for (i = 0; i < g_userCnt; i++)
    {
        if (g_users[i].id == uid) { idx = i; break; }
    }
    if (idx < 0)
    {
        printf("\n  账号信息异常，请重新登录。\n");
        pauseScreen();
        return;
    }

    while (1)
    {
        system("cls");
        printf("================= 账号设置 =================\n");
        printf("  当前昵称：%s\n", g_users[idx].name);
        printf("  用户编号：%d\n", g_users[idx].id);
        printf("----------------------------------------------\n");
        printf("   1. 修改昵称\n");
        printf("   2. 修改密码\n");
        printf("   0. 返回\n");
        printf("----------------------------------------------\n");
        printf("请选择 [0-2]：");
        op = inputInt(0, 2);

        if (op == 0) return;

        /* ---------- 改昵称 ---------- */
        if (op == 1)
        {
            printf("\n  新昵称（1~9 个汉字）：");
            inputStr(buf, sizeof(buf));

            if (strcmp(buf, g_users[idx].name) == 0)
            {
                printf("\n  和现在的昵称一样，没有改动。\n");
                pauseScreen();
                continue;
            }

            /* 用户名是登录凭据，必须唯一，所以要和别人比一遍（跳过自己） */
            for (i = 0; i < g_userCnt; i++)
            {
                if (i != idx && strcmp(g_users[i].name, buf) == 0) break;
            }
            if (i < g_userCnt)
            {
                printf("\n  昵称「%s」已经有人用了，换一个吧。\n", buf);
                pauseScreen();
                continue;
            }

            strcpy(g_users[idx].name, buf);
            strcpy(name, buf);              /* 菜单标题要立刻跟着变 */
            saveUsers();

            printf("\n  昵称已改为「%s」。\n", buf);
            printf("  说明：已经出过的票上仍记着原来的名字，\n");
            printf("        订单是下单那一刻的凭证，不随改名变动。\n");
            pauseScreen();
        }

        /* ---------- 改密码 ---------- */
        else if (op == 2)
        {
            printf("\n  请输入原密码：");
            inputPassword(oldPwd, sizeof(oldPwd));

            if (!checkPassword(oldPwd, g_users[idx].pwd, g_users[idx].id))
            {
                printf("\n  原密码不正确，密码没有改动。\n");
                pauseScreen();
                continue;
            }

            printf("  新密码（字母+数字+符号，不能含 $）：");
            inputPassword(pwd1, sizeof(pwd1));
            printf("  再输一次新密码：");
            inputPassword(pwd2, sizeof(pwd2));

            if (strcmp(pwd1, pwd2) != 0)
            {
                printf("\n  两次输入的新密码不一致，密码没有改动。\n");
                pauseScreen();
                continue;
            }
            if (strcmp(pwd1, oldPwd) == 0)
            {
                printf("\n  新密码和原密码相同，等于没改。\n");
                pauseScreen();
                continue;
            }
            if (!checkPwdStrength(pwd1, reason))
            {
                printf("\n  %s，密码没有改动。\n", reason);
                pauseScreen();
                continue;
            }

            /* 存哈希，不存明文：和注册走同一个函数，
               保证登录时算出来的值能对上 */
            hashPassword(pwd1, g_users[idx].id, g_users[idx].pwd);
            saveUsers();

            printf("\n  密码已更新，下次登录请用新密码。\n");
            pauseScreen();
        }
    }
}

/*-------------------- 4. 观众菜单 --------------------*/
/* uid：当前用户编号（购票/退票/查记录都要用）
   name：当前用户名（只用于显示） */
void userMenu(int uid, char *name)
{
    int op;
    int sIdx;

    while (1)
    {
        system("cls");
        printf("==============================================\n");
        printf("  观众：%s    今天 %s\n", name, g_today);
        printf("==============================================\n");
        printf("   1. 场次与座位图\n");
        printf("   2. 购票\n");
        printf("   3. 退票\n");
        printf("   4. 我的购票记录\n");
        printf("   5. 改签\n");
        printf("   6. 账号设置\n");
        printf("   0. 返回主菜单\n");
        printf("----------------------------------------------\n");
        printf("请选择 [0-6]：");
        op = inputInt(0, 6);

        if (op == 0) return;

        switch (op)
        {
        case 1:                                  /* 看场次 + 看座位，合并成一步 */
            sIdx = pickSchedule();               /* 自带在售列表；取消返回 -1 */
            if (sIdx >= 0)
            {
                showSeats(sIdx);
                printf("\n  提示：记住排号和座号，回菜单选「购票」就能下单。\n");
            }
            else
            {
                printf("\n  已取消，回到菜单。\n");
            }
            pauseScreen();
            break;

        case 2:
            buyTicket(uid);                      /* 写在 ticket.c */
            break;

        case 3:
            refundTicket(uid);                   /* 写在 ticket.c */
            break;

        case 4:
            showMyTickets(uid);                  /* 写在 ticket.c */
            break;

        case 5:
            changeTicket(uid);                   /* 写在 ticket.c */
            break;

        case 6:
            userProfile(uid, name);              /* 改名/改密码，name 就地更新 */
            break;
        }

        /* 这里故意不写 pauseScreen()：
           上面每个功能函数内部都已经暂停过一次，
           再暂停一次就成了"要点两下回车才回菜单" */
    }
}
