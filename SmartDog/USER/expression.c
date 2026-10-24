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
	expr_line_thick(x , y, x+ s, y+ s , t);          /* 斜线 */
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
	expr_round_rect_fill(70, 45, 12, 3, 2);                   /* 左闭眼 粗长 */
	expr_round_rect_fill(100, 45, 12, 3, 2);                  /* 右闭眼 粗长 */
	expr_Z(15, 2,  13, 1);                                  /* 大Z */
	expr_Z(35, 17, 9,  1);                                  /* 中Z */
	expr_Z(50, 28, 6,  0);                                  /* 小z */
}

/* ====================================================================
 * ==================== 抽奖转盘(非阻塞状态机) ====================
 * 收到 CMD_LOTTERY 后由 func.c 停动作+立正, 再调 lottery_start();
 * 主机循环每轮调 lottery_poll() 按毫秒差值推进, 不阻塞。
 * 转盘6个扇区(各60deg), 从"谢谢惠顾"区(正下方)开始顺时针逐区点亮,
 * 每区~166ms, 共转 6 圈(36 步、每圈~1s)后停在"谢谢惠顾", 向 USART3 发结果帧。
 * 坐标: 逻辑(x,y)以左上为原点, x 向右 y 向下; lot_dot() 映射到 GRAM(127-x, 63-y)
 *       (抽奖画面需垂直翻转), 保证中文不倒。
 * ====================================================================*/

/* 逻辑画点: 翻到 GRAM 坐标(127-x, 63-y)——抽奖需垂直翻转, 越界丢弃 */
static void lot_dot(int x, int y, u8 on)
{
	if (x >= 0 && x < 128 && y >= 0 && y < 64)
		OLED_DrawPoint((u8)(127 - x), (u8)(63 - y), on);
}

/* (直线绘制 lot_line 已移除: 卡片式布局不再需要) */

/* ============ 中文字模: 12x12 原生点阵, 每字 24 字节(12行 x 每行2字节) ============
 * 每行: 高字节=左8列(bit7=最左), 低字节高4位=右4列(bit7..bit4); row0=顶行。
 * 由系统宋体(SimSun)渲染提取生成(tmp/genfont12.ps1); 若需更精细可重跑替换。 */
#define G_CHAO  0   /* 钞 */
#define G_PIAO  1   /* 票 */
#define G_JIN   2   /* 金 */
#define G_TIAO  3   /* 条 */
#define G_SHOU  4   /* 手 */
#define G_BIAO  5   /* 表 */
#define G_XIE   6   /* 谢 */
#define G_HUI   7   /* 惠 */
#define G_GU    8   /* 顾 */
#define G_BA    9   /* 巴 */
#define G_LEI  10   /* 雷 */
#define G_TE   11   /* 特 */
#define G_BEN  12   /* 奔 */
#define G_CHI  13   /* 驰 */

static const u8 g_lot_font[14][24] = {
	/* 钞 */ {0x10,0x40,0x10,0x40,0x1C,0x50,0x21,0x40,0x3D,0x40,0x11,0x40,0x12,0x40,0x3C,0x40,0x10,0x10,0x14,0x20,0x18,0xC0,0x13,0x00},
	/* 票 */ {0x3F,0xF0,0x02,0x80,0x1F,0xF0,0x12,0x90,0x1F,0xF0,0x00,0x00,0x1F,0xF0,0x00,0x00,0x3F,0xF0,0x01,0x00,0x09,0x20,0x13,0x10},
	/* 金 */ {0x01,0x00,0x02,0x80,0x04,0x40,0x08,0x20,0x37,0xD0,0x01,0x00,0x01,0x00,0x1F,0xF0,0x01,0x00,0x09,0x20,0x05,0x40,0x3F,0xF0},
	/* 条 */ {0x04,0x00,0x0F,0xE0,0x14,0x40,0x03,0x80,0x0C,0x60,0x31,0x10,0x01,0x00,0x1F,0xF0,0x01,0x00,0x09,0x20,0x11,0x10,0x23,0x00},
	/* 手 */ {0x00,0xF0,0x1F,0x00,0x01,0x00,0x01,0x00,0x1F,0xF0,0x01,0x00,0x01,0x00,0x3F,0xF0,0x01,0x00,0x01,0x00,0x01,0x00,0x07,0x00},
	/* 表 */ {0x01,0x00,0x01,0x00,0x1F,0xF0,0x01,0x00,0x0F,0xE0,0x01,0x00,0x3F,0xF0,0x02,0x80,0x04,0x80,0x0C,0x50,0x35,0x20,0x06,0x10},
	/* 谢 */ {0x22,0x10,0x17,0x90,0x04,0x90,0x07,0xB0,0x34,0x90,0x17,0x90,0x14,0xD0,0x1F,0xB0,0x12,0x90,0x14,0x90,0x18,0x90,0x11,0xB0},
	/* 惠 */ {0x01,0x00,0x3F,0xF0,0x01,0x00,0x0F,0xE0,0x09,0x20,0x0F,0xE0,0x09,0x20,0x3F,0xF0,0x01,0x00,0x14,0x90,0x14,0x20,0x23,0xE0},
	/* 顾 */ {0x00,0x00,0x3E,0xF0,0x20,0x20,0x2E,0x40,0x2A,0xF0,0x2A,0x80,0x2A,0xA0,0x2E,0xA0,0x28,0xA0,0x2A,0x20,0x2C,0x50,0x08,0x80},
	/* 巴 */ {0x00,0x00,0x1F,0xF0,0x11,0x10,0x11,0x10,0x11,0x10,0x11,0x10,0x1F,0xF0,0x10,0x00,0x10,0x00,0x10,0x00,0x10,0x00,0x0F,0xF0},
	/* 雷 */ {0x1F,0xF0,0x01,0x00,0x3F,0xF0,0x2D,0x60,0x01,0x00,0x1D,0x70,0x00,0x00,0x1F,0xF0,0x11,0x10,0x1F,0xF0,0x11,0x10,0x1F,0xF0},
	/* 特 */ {0x08,0x40,0x08,0x40,0x29,0xF0,0x3C,0x40,0x2B,0xF0,0x08,0x20,0x0D,0xF0,0x38,0x20,0x09,0x20,0x08,0xA0,0x08,0x20,0x08,0xE0},
	/* 奔 */ {0x01,0x00,0x02,0x00,0x1F,0xF0,0x04,0x40,0x09,0x20,0x37,0xD0,0x01,0x00,0x04,0x40,0x3F,0xF0,0x04,0x40,0x08,0x40,0x10,0x40},
	/* 驰 */ {0x00,0x20,0x3C,0x20,0x04,0xA0,0x14,0xB0,0x14,0xE0,0x15,0xA0,0x1E,0xA0,0x02,0xB0,0x0A,0xA0,0x32,0x80,0x02,0x80,0x0C,0x70}
};

/* 在(ox,oy)画一个12x12字: 每行2字节(高字节左8列, 低字节高4位=右4列); on=1点亮, 0=熄灭/描黑 */
static void lot_glyph(u8 id, int ox, int oy, u8 on)
{
	int r, c, bit;
	for (r = 0; r < 12; r++)
		for (c = 0; c < 12; c++) {
			if (c < 8) bit = (g_lot_font[id][r * 2]     >> (7 - c))  & 0x01;
			else       bit = (g_lot_font[id][r * 2 + 1] >> (15 - c)) & 0x01;
			/* 行/列序均反转以补偿(127-x,63-y)的180度旋转: 使文字正立 */
			if (bit) lot_dot(ox + (11 - c), oy + (11 - r), on);
		}
}

/* ===== 卡片式转盘: 6张圆角矩形卡围绕中心, 每卡横排显示完整奖词 =====
 * 卡位索引 i 对应中心角(区0=正上方谢谢惠顾, 抽奖停在此); 选中卡实心+黑字, 其余描边+白字。 */
static const int LOT_CX = 64, LOT_CY = 32;
#define LOT_CH_W   12   /* 字宽高 */
#define LOT_CH_GAP  1   /* 字间距 */
#define LOT_PAD_X   2   /* 卡片左右内边距 */
#define LOT_PAD_Y   1   /* 卡片上下内边距 */
#define LOT_RAD     3   /* 圆角半径 */
/* 6卡中心(逻辑), 手工排布尽量少重叠: 上/右上/右下/下/左下/左上 */
static const int s_cd_cx[6] = { 64, 100, 100, 68, 28, 28 };
static const int s_cd_cy[6] = { 8, 26, 44, 56, 44, 26 };
/* 每字完整奖词(字模ID), 末尾补0; 长度见 s_word_len */
static const u8 s_word_len[6] = { 4, 2, 2, 2, 3, 2 };
static const u8 s_word[6][4] = {
	{ G_XIE,  G_XIE,  G_HUI, G_GU },  /* 谢谢惠顾(现放正上方) */
	{ G_JIN,  G_TIAO, 0,     0    },  /* 金条 */
	{ G_SHOU, G_BIAO, 0,     0    },  /* 手表 */
	{ G_CHAO, G_PIAO, 0,     0    },  /* 钞票(现放正下方) */
	{ G_BA,   G_LEI,  G_TE,  0    },  /* 巴雷特 */
	{ G_BEN,  G_CHI,  0,     0    }   /* 奔驰 */
};

/* 点(x,y)是否落在中心(cx,cy)半轴(hw,hh)的圆角矩形内(rad=圆角半径) */
static int lot_in_rrect(int x, int y, int cx, int cy, int hw, int hh, int rad)
{
	int ax = x - cx, ay = y - cy, ox, oy;
	if (ax < 0) ax = -ax;
	if (ay < 0) ay = -ay;
	if (ax > hw || ay > hh) return 0;
	ox = ax - (hw - rad); if (ox < 0) ox = 0;
	oy = ay - (hh - rad); if (oy < 0) oy = 0;
	return (ox * ox + oy * oy <= rad * rad) ? 1 : 0;
}

/* 画圆角矩形: fill=1 实心点亮; fill=0 仅描1像素边 */
static void lot_round_rect(int cx, int cy, int hw, int hh, int rad, int fill)
{
	int x, y, edge;
	for (y = cy - hh; y <= cy + hh; y++)
		for (x = cx - hw; x <= cx + hw; x++) {
			if (!lot_in_rrect(x, y, cx, cy, hw, hh, rad)) continue;
			if (fill) { lot_dot(x, y, 1); continue; }
			edge = !lot_in_rrect(x + 1, y, cx, cy, hw, hh, rad) ||
			       !lot_in_rrect(x - 1, y, cx, cy, hw, hh, rad) ||
			       !lot_in_rrect(x, y + 1, cx, cy, hw, hh, rad) ||
			       !lot_in_rrect(x, y - 1, cx, cy, hw, hh, rad);
			if (edge) lot_dot(x, y, 1);
		}
}

/* ===== 外框椭圆: 纯整数隐式方程描1像素边, 尽量圈住所有卡片 =====
 * 受 64 高屏限制, 上下几乎被卡占满, 侧卡外角可能略微顶出; 尺寸可调。 */
#define LOT_EL_CX  63
#define LOT_EL_CY  32
#define LOT_EL_RX  64
#define LOT_EL_RY  40

/* 点(x,y)是否在以(cx,cy)半轴(rx,ry)的椭圆内(含边界) */
static int lot_in_ellipse(int x, int y, int cx, int cy, int rx, int ry)
{
	long dx = x - cx, dy = y - cy;
	return (dx * dx * ry * ry + dy * dy * rx * rx) <= ((long)rx * rx * ry * ry) ? 1 : 0;
}

/* 描椭圆轮廓(1像素): 内点且4邻域有点在椭圆外者 */
static void lot_ellipse(int cx, int cy, int rx, int ry)
{
	int x, y, edge;
	for (y = cy - ry; y <= cy + ry; y++)
		for (x = cx - rx; x <= cx + rx; x++) {
			if (!lot_in_ellipse(x, y, cx, cy, rx, ry)) continue;
			edge = !lot_in_ellipse(x + 1, y, cx, cy, rx, ry) ||
			       !lot_in_ellipse(x - 1, y, cx, cy, rx, ry) ||
			       !lot_in_ellipse(x, y + 1, cx, cy, rx, ry) ||
			       !lot_in_ellipse(x, y - 1, cx, cy, rx, ry);
			if (edge) lot_dot(x, y, 1);
		}
}

/* 绘制一帧: 6张圆角卡环形排布, 卡内横排完整奖词, 选中卡高亮 */
static void lot_render(int cur)
{
	int i, j, n, cx, cy, hw, hh, tw, x0, fill, tcol;
	OLED_ClearBuf();   /* 只清GRAM不刷新, 绘制完统一一次刷新 */
	for (i = 0; i < 6; i++) {
		n    = s_word_len[i];
		tw   = n * LOT_CH_W + (n - 1) * LOT_CH_GAP;
		hw   = tw / 2 + LOT_PAD_X;
		hh   = LOT_CH_W / 2 + LOT_PAD_Y;
		cx   = s_cd_cx[i];
		cy   = s_cd_cy[i];
		fill = (i == cur) ? 1 : 0;
		lot_round_rect(cx, cy, hw, hh, LOT_RAD, fill);
		tcol = fill ? 0 : 1;   /* 选中: 白底黑字; 其余: 黑底白字 */
		x0   = cx - tw / 2;
		for (j = 0; j < n; j++)
			lot_glyph(s_word[i][j], x0 + (n - 1 - j) * (LOT_CH_W + LOT_CH_GAP),
			          cy - LOT_CH_W / 2, tcol);   /* (n-1-j)反转字序, 补偿lot_dot的127-x左右翻转 */
	}
	lot_ellipse(LOT_EL_CX, LOT_EL_CY, LOT_EL_RX, LOT_EL_RY);   /* 外框椭圆画在卡片之上 */
	OLED_Refresh();
}

#define LOT_START_IDX   0        /* 从"谢谢惠顾"区(现正上方)开始, 转整数圈后必停回此处 */
#define LOT_STEP_MS     (1000u / 20u)  /* 每区50ms: 每圈6区耗时150ms */
#define LOT_LOOPS       24       /* 默认转 24 圈 */
#define LOT_DONE_MS     5000u    /* 停在结果后停留3s再发送 */

static u8  s_lot_active = 0;
static u8  s_lot_phase  = 0;   /* 0空闲 1轮选 2停留 */
static int s_lot_cur    = LOT_START_IDX;
static u16 s_lot_steps  = 0;
static u16 s_lot_total_steps = LOT_LOOPS * 6;   /* 本次抽奖的换区总步数(启动时设定) */
static u32 s_lot_step_tick = 0;
static u32 s_lot_done_tick = 0;

u8 lottery_is_active(void) { return s_lot_active; }

/* 启动转盘: total_steps 为换区总步数; 每圈6步, 从区0(谢谢惠顾)出发后 N*6 步必定回到区0 */
static void lottery_begin(u16 total_steps)
{
	s_lot_active      = 1;
	s_lot_phase       = 1;
	s_lot_cur         = LOT_START_IDX;
	s_lot_steps       = 0;
	s_lot_total_steps = total_steps;
	s_lot_step_tick   = g_sys_tick_ms;
	lot_render(s_lot_cur);
}

void lottery_start(void)       { lottery_begin(LOT_LOOPS * 6); }  /* 6圈(36步), 每圈~1s */
void lottery_start_once(void)  { lottery_begin(6); }              /* 单圈(6步), 转完一轮停在谢谢惠顾 */

void lottery_poll(void)
{
	if (!s_lot_active) return;

	if (s_lot_phase == 1) {
		if (g_sys_tick_ms - s_lot_step_tick >= LOT_STEP_MS) {
			s_lot_step_tick = g_sys_tick_ms;
			s_lot_cur = (s_lot_cur + 1) % 6;
			s_lot_steps++;
			if (s_lot_steps >= s_lot_total_steps) {
				s_lot_phase = 2;
				s_lot_done_tick = g_sys_tick_ms;
			}
			lot_render(s_lot_cur);
		}
	} else if (s_lot_phase == 2) {
		if (g_sys_tick_ms - s_lot_done_tick >= LOT_DONE_MS) {
			u8 code = 0x06;
			USART3_SendFrame(CMD_EXPRESSION, &code, 1); /* AA 55 31 01 06 37 0D 0A */
			s_lot_active = 0;
			s_lot_phase  = 0;
			g_expr_dirty = 1;   /* 请求回位表情, 由主机循环 expression_poll 刷新 */
		}
	}
}
