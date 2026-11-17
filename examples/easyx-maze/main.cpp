#include <graphics.h>
#include <conio.h>
#include <stdio.h>
#include <windows.h> // 必须加这个头文件，PeekMessage 就在这里

int main()
{
    int ball_x = -180;
    int ball_y = -180;

    initgraph(1000, 1000);
    setorigin(500, 500);

    // ==========================================
    // 1. 画蓝线迷宫和红叉
    // ==========================================
    setlinecolor(BLUE);
    setlinestyle(PS_SOLID, 20);
    line(-180, -180, 180, -180);
    line(-180, 180, 180, 180);
    line(-180, -180, -180, 180);
    line(180, -180, 180, 180);
    line(-100, -180, -100, -100);
    line(-100, -100, -180, -100);
    line(100, -180, 100, -100);
    line(100, -100, 180, -100);
    line(100, -100, 100, 100);
    line(-180, -20, -20, -20);
    line(-20, -180, -20, 50);
    line(-180, 60, 100, 60);
    line(-100, 60, -100, 180);
    line(-180, 140, -100, 140);
    line(-20, 60, -20, 180);
    line(-20, 100, 100, 100);

    setlinecolor(RED);
    setlinestyle(PS_SOLID, 10);
    line(185, -185, 175, -175);
    line(185, -175, 175, -185);

    // 画初始黄球
    setfillcolor(YELLOW);
    solidcircle(ball_x, ball_y, 10);

    // ==========================================
    // 2. 用 PeekMessage 替换原来的 while(1)
    // ==========================================
    MSG msg; // 定义消息结构体

    while (1)
    {
        // --- 核心修改点：使用 PeekMessage 读取按键 ---
        // PeekMessage 意思是“偷偷看一眼”，如果没有按键它不会卡住程序，会直接往下走
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
            // 处理点右上角 X 关闭窗口
            if (msg.message == WM_DESTROY || msg.message == WM_CLOSE) {
                closegraph();
                return 0;
            }

            // 处理键盘按下事件
            if (msg.message == WM_KEYDOWN)
            {
                int next_x = ball_x;
                int next_y = ball_y;

                // 使用 VK_ 编码，比 224 高级多了，方向键和WASD都能认！
                switch (msg.wParam)
                {
                    case VK_ESCAPE: // 退出
                        closegraph();
                        return 0;
                    case 'W': case VK_UP:    next_y -= 20; break; // 上
                    case 'S': case VK_DOWN:  next_y += 20; break; // 下
                    case 'A': case VK_LEFT:  next_x -= 20; break; // 左
                    case 'D': case VK_RIGHT: next_x += 20; break; // 右
                }

                // 边界限制
                if (next_x >= -180 && next_x <= 180 && next_y <= 180 && next_y >= -180)
                {
                    // 用黑球擦掉旧位置
                    setfillcolor(BLACK);
                    solidcircle(ball_x, ball_y, 12);

                    // 更新坐标并画新球
                    ball_x = next_x;
                    ball_y = next_y;
                    setfillcolor(YELLOW);
                    solidcircle(ball_x, ball_y, 10);
                }
            }

            // 必须加这两句（死规矩），用于翻译和分发系统消息
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        Sleep(10); // 电脑休息，防止卡死
    }

    closegraph();
    return 0;
}
