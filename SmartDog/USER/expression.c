#include "expression.h"

/* 表情状态变量定义在func.c,此处通过expression.h的extern声明使用 */

/* ==================== 表情绘制 ====================
 * 全部用图元直接写入 OLED_GRAM，不保存任何位图数组(0额外字节RAM/Flash)。
 * 纯整数运算，不使用<math.h>与浮点。绘制后统一由 OLED_Refresh() 刷新一次。
 * 画布 128x64；已去除嘴巴与Zzz，改为放大眼睛表现情绪。
 */
static void expr_dot_p(int x, int y, u8 p)       /* 带越界保护的画点(p=1点亮,0熄灭) */
{
	if(x >= 0 && x < 128 && y >= 0 && y < 64)
		OLED_DrawPoint((u8)(127 - x), (u8)(y), p); /* DisplayTurn(1)后GRAM x=0在右,翻转X;Y轴上下翻转:逻辑y直接映射GRAM y */
}
static void expr_dot(int x, int y) { expr_dot_p(x, y, 1); }

static void expr_line(int x0, int y0, int x1, int y1)/* Bresenham直线 */
{
	int dx = x1 - x0, dy = y1 - y0;
	int sx = (dx >= 0) ? 1 : -1;
	int sy = (dy >= 0) ? 1 : -1;
	int err, e2;
	if(dx < 0) dx = -dx;
	if(dy < 0) dy = -dy;
	err = dx - dy;
	while(1){
		expr_dot(x0, y0);
		if(x0 == x1 && y0 == y1) break;
		e2 = 2 * err;
		if(e2 > -dy){ err -= dy; x0 += sx; }
		if(e2 <  dx){ err += dx; y0 += sy; }
	}
}

static void expr_line_thick(int x0,int y0,int x1,int y1,int t)/* 加粗直线(横向偏移t) */
{
	int i;
	for(i = -t; i <= t; i++)
		expr_line(x0 + i, y0, x1 + i, y1);
}

static void expr_fill_circle(int cx, int cy, int r)/* 实心圆 */
{
	int dx, dy;
	for(dy = -r; dy <= r; dy++)
		for(dx = -r; dx <= r; dx++)
			if(dx*dx + dy*dy <= r*r) expr_dot(cx + dx, cy + dy);
}

static void expr_ring(int cx, int cy, int r)       /* 约2px厚空心圆 */
{
	int dx, dy, d;
	for(dy = -r; dy <= r; dy++)
		for(dx = -r; dx <= r; dx++){
			d = dx*dx + dy*dy;
			if(d <= r*r && d >= (r-2)*(r-2)) expr_dot(cx + dx, cy + dy);
		}
}

static u8 expr_rr_inside(int a, int b, int hx, int hy, int r)/* 点(相对中心)是否在圆角矩形内,半宽hx/半高hy */
{
	int ax = (a < 0) ? -a : a;
	int ay = (b < 0) ? -b : b;
	int cx, cy;
	if(ax > hx || ay > hy) return 0;
	cx = ax - (hx - r); cy = ay - (hy - r);      /* 落入角上的圆控制区 */
	if(cx > 0 && cy > 0) return (cx*cx + cy*cy <= r*r);
	return 1;
}

static void expr_round_square(int cx, int cy, int h, int r)/* 约2px厚圆角正方形轮廓 */
{
	int dy, dx;
	int ri = (r > 2) ? r - 2 : 1;                /* 内沿圆角同步缩小保持等厚 */
	for(dy = -h; dy <= h; dy++)
		for(dx = -h; dx <= h; dx++)
			if(expr_rr_inside(dx, dy, h, h, r) && !expr_rr_inside(dx, dy, h - 2, h - 2, ri))
				expr_dot(cx + dx, cy + dy);
}

static void expr_round_rect_fill(int cx, int cy, int hx, int hy, int r)/* 实心圆角矩形(半宽hx,半高hy) */
{
	int dy, dx;
	for(dy = -hy; dy <= hy; dy++)
		for(dx = -hx; dx <= hx; dx++)
			if(expr_rr_inside(dx, dy, hx, hy, r)) expr_dot(cx + dx, cy + dy);
}

static void expr_round_square_fill(int cx, int cy, int h, int r)/* 实心圆角正方形 */
{
	expr_round_rect_fill(cx, cy, h, h, r);
}

/* 把[xa,xb]各列中高于眉毛斜线的像素撞灭(眉线两端外按端点延长) */
static void expr_clear_above(int xa, int xb, int lx0, int ly0, int lx1, int ly1)
{
	int x, y, yb, xi;
	int lo = (lx0 < lx1) ? lx0 : lx1;
	int hi = (lx0 < lx1) ? lx1 : lx0;
	for(x = xa; x <= xb; x++){
		xi = (x < lo) ? lo : ((x > hi) ? hi : x);
		yb = ly0 + (ly1 - ly0) * (xi - lx0) / (lx1 - lx0);
		for(y = 0; y < yb; y++)
			expr_dot_p(x, y, 0);
	}
}

/* 字母Z: 左上角(x,y), 边长s, 上横-斜线-下横三笔,线厚t */
static void expr_Z(int x, int y, int s, int t)
{
	expr_line_thick(x, y, x + s, y, t);              /* 上横 */
	expr_line_thick(x + s, y, x, y + s, t);          /* 斜线 */
	expr_line_thick(x, y + s, x + s, y + s, t);      /* 下横 */
}

/* 凸上凹下弧形条(眼笑成拱): 宽=2w, 每列上/下边界都是椭圆弧(半宽w,半高h);
 * t=弯条竖直半厚, (cx,cy)=边缘列中心 */
static void expr_smile_band(int cx, int cy, int w, int h, int t)
{
	int dx, dy, s;
	for(dx = -w; dx <= w; dx++){
		for(s = h; w*w*s*s > h*h*(w*w - dx*dx); s--);  /* 椭圆: (dx/w)²+(s/h)²=1 */
		for(dy = -t; dy <= t; dy++)
			expr_dot(cx + dx, cy - s + dy);
	}
}

/* ==================== 大眼表情(已去除嘴巴与Zzz) ====================
 * 眼睛放大: 左眼中心(40,34) 右眼中心(88,34), 环径 r=12~14
 * 靠眼睛轮廓 + 眉毛/泪滴/眼缝形态区分情绪, 不再画嘴
 */
void expr_face_normal(void)        /* 正常: 两个实心圆角正方形眼睛(可随转弯水平偏移) */
{
	expr_round_square_fill(32 + g_expr_shift, 34, 20, 4);  /* 左眼 */
	expr_round_square_fill(90 + g_expr_shift, 34, 20, 4);  /* 右眼 */
}

void expr_face_happy(void)         /* 开心: 矮弧笑眼(宽保持20,弧高缩小) */
{
	expr_smile_band(32, 34, 20, 14, 4);             /* 左眼: w=20 h=12 t=4 */
	expr_smile_band(90, 34, 20, 14, 4);             /* 右眼 */
}

void expr_face_cry(void)           /* 哭泣: 15高X20宽圆角矩形眼 + 囧形拱眉 + 左眼下一滴小泪 */
{
	expr_round_rect_fill(35, 28, 20, 15, 5);      /* 左眼: 半宽20 半高15 */
	expr_round_rect_fill(93, 28, 20, 15, 5);      /* 右眼 */
	/* 囧形眉: 整张脸中间高、两端外低 */
	expr_clear_above(15, 55, 20, 20, 50, 12);     /* 左眼全宽按眉线剪裁(两端外按端点延长) */
	expr_clear_above(73, 113, 78, 12, 108, 20);   /* 右眼全宽对称剪裁 */
	expr_line(20, 20, 50, 12);                    /* 左眉: 外低内高,向眉心抬起 */
	expr_line(78, 12, 108, 20);                   /* 右眉: 内高外低,镜像 */
	expr_line_thick(30, 46, 30, 48, 1); expr_fill_circle(30, 54, 4); /* 左眼下一滴小泪珠 */
}

void expr_face_daze(void)          /* 发呆: 大瞪眼(空心圈+小瞳) */
{
	expr_ring(35, 34, 20); expr_fill_circle(35, 34, 5);   /* 左瞪眼 */
	expr_ring(93, 34, 20); expr_fill_circle(93, 34, 5);   /* 右瞪眼 */
}

void expr_face_angry(void)         /* 生气: 圆角方块眼(与正常同款) + 倒八字勾型压眉 */
{
	expr_round_rect_fill(30, 30, 20, 15, 6);        /* 左眼 */
	expr_round_rect_fill(88, 30, 20, 15, 6);        /* 右眼 */
	expr_clear_above(10, 50, 13, 16, 41, 28);     /* 左眼高出眉线的部分熄灭(眉毛y下移5) */
	expr_clear_above(68, 108, 105, 16, 77, 28);   /* 右眼对称剪裁 */
	expr_line_thick(13, 16, 41, 28, 2);           /* 左眉: 外高内低,眉尾压上眼沿 */
	expr_line_thick(41, 28, 41, 36, 1);           /* 左眉内端下勾 */
	expr_line_thick(105, 16, 77, 28, 2);          /* 右眉镜像 */
	expr_line_thick(77, 28, 77, 36, 1);           /* 右眉内端下勾 */
}

void expr_face_sleep(void)         /* 睡觉: 两条粗闭眼横线 + 右上角递减三个Z字 */
{
	expr_round_rect_fill(35, 44, 10, 5, 2);                   /* 左闭眼 粗长 */
	expr_round_rect_fill(60, 44, 10, 5, 2);                  /* 右闭眼 粗长 */
	expr_Z(110, 2,  13, 1);                                  /* 大Z */
	expr_Z(95, 17, 9,  1);                                  /* 中Z */
	expr_Z(85, 28, 6,  0);                                  /* 小z */
}
