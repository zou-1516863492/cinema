#define _CRT_SECURE_NO_WARNINGS
#include "cinema.h"
#include <stdio.h>
#include <string.h>

int adminLogin(void)
{
    char pwd[20];

    system("cls");
    printf("============ 管理员登录 ============\n");
    printf("请输入管理员密码：");
    inputPassword(pwd, sizeof(pwd));

    if (strcmp(pwd, ADMIN_PWD) == 0)
    {
        printf("登录成功！\n");
        pauseScreen();
        return 1;
    }

    printf("密码错误！\n");
    pauseScreen();
    return 0;
}

void adminMenu(void)
{
    int op;

    while (1)
    {
        system("cls");
        printf("==============================================\n");
        printf("                管理员菜单\n");
        printf("==============================================\n");
        printf("   1. 添加电影\n");
        printf("   2. 查看电影\n");
        printf("   3. 添加场次\n");
        printf("   4. 查看场次\n");
        printf("   5. 票房统计\n");
        printf("   6. 票房排序\n");
        printf("   0. 返回主菜单\n");
        printf("----------------------------------------------\n");
        printf("请选择 [0-6]：");

        op = inputInt(0, 6);

        switch (op)
        {
        case 1: addMovie();           
            break;
        case 2: showMovies();         
            break;
        case 3: addSchedule();        
            break;
        case 4: showSchedules();      
            break;
        case 5: calculateBoxOffice(); 
            break;
        case 6: sortBoxOffice();      
            break;
        case 0: return;
        }
    }
}


