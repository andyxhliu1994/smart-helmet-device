#include "osd.h"
#include "log.h"

FT_Library library_;
FT_Face face_;
FT_GlyphSlot slot_;
FT_Vector pen_;
double font_angle_;
int font_size_;
unsigned int font_color_;
char *font_path_[128];
unsigned int color_index_;
unsigned int trans_index_;
static pthread_mutex_t g_font_mutex = PTHREAD_MUTEX_INITIALIZER;

/**************************************************************** */
RK_S32 RGN_ChangePosition(RGN_HANDLE RgnHandle, const MPP_CHN_S *pstChn, RK_S32 s32X, RK_S32 s32Y) {
    RGN_CHN_ATTR_S stChnAttr;
    RK_S32 s32Ret = RK_SUCCESS;

    if (RK_NULL == pstChn) {
        RK_LOGE("input parameter is null. it is invaild!");
        return RK_FAILURE;
    }

    s32Ret = RK_MPI_RGN_GetDisplayAttr(RgnHandle, pstChn, &stChnAttr);
    if (RK_SUCCESS != s32Ret) {
        RK_LOGE("RK_MPI_RGN_GetDisplayAttr (%d)) failed with %#x!", RgnHandle, s32Ret);
        return RK_FAILURE;
    }

    switch (stChnAttr.enType) {
      case OVERLAY_RGN: {
        stChnAttr.unChnAttr.stOverlayChn.stPoint.s32X = s32X;
        stChnAttr.unChnAttr.stOverlayChn.stPoint.s32Y = s32Y;
      } break;
      case COVER_RGN: {
        stChnAttr.unChnAttr.stCoverChn.stRect.s32X = s32X;
        stChnAttr.unChnAttr.stCoverChn.stRect.s32Y = s32Y;
      } break;
      case MOSAIC_RGN: {
        if (AREA_RECT == stChnAttr.unChnAttr.stMosaicChn.enMosaicType) {
            stChnAttr.unChnAttr.stMosaicChn.stRect.s32X = s32X;
            stChnAttr.unChnAttr.stMosaicChn.stRect.s32Y = s32Y;
        } else if (AREA_QUAD_RANGLE == stChnAttr.unChnAttr.stMosaicChn.enMosaicType) {
            for (RK_S32 i = 0; i < 4; i++) {
                stChnAttr.unChnAttr.stMosaicChn.stQuadRangle.stPoint[i].s32X += s32X;
                stChnAttr.unChnAttr.stMosaicChn.stQuadRangle.stPoint[i].s32Y += s32Y;
            }
        }
      } break;
      case LINE_RGN: {
        stChnAttr.unChnAttr.stLineChn.stStartPoint.s32X = s32X;
        stChnAttr.unChnAttr.stLineChn.stStartPoint.s32Y = s32Y;
      } break;
      default:
        break;
    }
    s32Ret = RK_MPI_RGN_SetDisplayAttr(RgnHandle, pstChn, &stChnAttr);
    if (RK_SUCCESS != s32Ret) {
        RK_LOGE("RK_MPI_RGN_SetDisplayAttr (%d)) failed with %#x!", RgnHandle, s32Ret);
        return RK_FAILURE;
    }

    return s32Ret;
}

RK_S32 RGN_ShowOrHide(RGN_HANDLE RgnHandle, const MPP_CHN_S *pstChn, RK_BOOL bShow) {
    RGN_CHN_ATTR_S stChnAttr;
    RK_S32 s32Ret = RK_SUCCESS;

    if (RK_NULL == pstChn) {
        RK_LOGE("input parameter is null. it is invaild!");
        return RK_FAILURE;
    }

    s32Ret = RK_MPI_RGN_GetDisplayAttr(RgnHandle, pstChn, &stChnAttr);
    if (RK_SUCCESS != s32Ret) {
        RK_LOGE("RK_MPI_RGN_GetDisplayAttr (%d)) failed with %#x!", RgnHandle, s32Ret);
        return RK_FAILURE;
    }

    stChnAttr.bShow = bShow;

    s32Ret = RK_MPI_RGN_SetDisplayAttr(RgnHandle, pstChn, &stChnAttr);
    if (RK_SUCCESS != s32Ret) {
        RK_LOGE("RK_MPI_RGN_SetDisplayAttr (%d)) failed with %#x!", RgnHandle, s32Ret);
        return RK_FAILURE;
    }

    return RK_SUCCESS;
}

/*************************************************************** */
int interval_offset_ = 0;
void draw_solid_border(uint32_t *buffer, BorderInfo info) {
	int offset;
	int rect_w = info.rect.w;
	int rect_h = info.rect.h;
	int thick = info.thick;
	uint32_t color = info.color;
	uint32_t dotted_line[info.rect.w];

	for (int i = 0; i < info.rect.w; i++) 
		dotted_line[i] = color;
	
	for (int j = 0; j < thick; j++) {
		offset = j * rect_w;
		memcpy(buffer + offset, dotted_line, rect_w * 4);
		offset = (rect_h - j - 1) * rect_w;
		memcpy(buffer + offset, dotted_line, rect_w * 4);
	}

	for (int j = 0; j < rect_h; j++) {
		for (int k = 0; k < thick; k++) {
			buffer[j * rect_w + k] = color;
			buffer[(j + 1) * rect_w - thick + k] = color;
		}
	}
}

void draw_dotted_border(uint32_t *buffer, BorderInfo info) {
	int offset;
	int rect_w = info.rect.w;
	int rect_h = info.rect.h;
	int thick = info.thick;
	uint32_t color = info.color;
	uint32_t color_key = info.color_key;
	int interval = info.interval;
	uint32_t dotted_line[info.rect.w];

	for (int k = 0; k < rect_w; k++) {
		if (((k + interval_offset_) / interval) % 2)
			dotted_line[k] = color;
		else
			dotted_line[k] = color_key;
	}

	//按线宽，画上下两条
	for (int j = 0; j < thick; j++) {
		for (int k = 0; k < rect_w; k++) {
			offset = j * rect_w;
			memcpy(buffer + offset, dotted_line, rect_w * 4);
			offset = (rect_h - j - 1) * rect_w;
			memcpy(buffer + offset, dotted_line, rect_w * 4);
		}
	}
	//按线宽，画左右两条
	for (int j = 0; j < rect_h; j++) {
		for (int k = 0; k < thick; k++) {
			if (((j + interval_offset_) / interval) % 2) {
				buffer[j * rect_w + k] = color;
				buffer[(j + 1) * rect_w - thick + k] = color;
			}
		}
	}
}

void draw_border(uint32_t *buffer, BorderInfo info) {
	if (info.display_style == BORDER_DOTTED) {
		interval_offset_ = 0;
		draw_dotted_border(buffer, info);
	} else if (info.display_style == BORDER_LINE) {
		draw_solid_border(buffer, info);
	} else if (info.display_style == BORDER_WATERFULL_LIGHT) {
		interval_offset_ += 40;
		draw_dotted_border(buffer, info);
	}
}


/*************************************************************** */
int create_font(const char *font_path, int font_size) {
	pthread_mutex_lock(&g_font_mutex);
	FT_Init_FreeType(&library_);
	if (!library_) {
		LOG_ERROR("FT_Init_FreeType fail\n");
		pthread_mutex_unlock(&g_font_mutex);
		return -1;
	}
	FT_New_Face(library_, font_path, 0, &face_);
	if (!face_) {
		LOG_ERROR("please check font_path %s\n", font_path);
		FT_Done_FreeType(library_);
		library_ = NULL;
		pthread_mutex_unlock(&g_font_mutex);
		return -1;
	}

	FT_Set_Pixel_Sizes(face_, font_size, 0);
	font_size_ = font_size;
	memcpy(font_path_, font_path, strlen(font_path));
	slot_ = face_->glyph;
	pthread_mutex_unlock(&g_font_mutex);

	return 0;
}

int destroy_font() {
	pthread_mutex_lock(&g_font_mutex);
	if (face_) {
		FT_Done_Face(face_);
		face_ = NULL;
	}
	if (library_) {
		FT_Done_FreeType(library_);
		library_ = NULL;
	}
	pthread_mutex_unlock(&g_font_mutex);
	return 0;
}

int set_font_size(int font_size) {
	if (!face_) {
		LOG_ERROR("face_ is null\n");
		return -1;
	}
	pthread_mutex_lock(&g_font_mutex);
	FT_Set_Pixel_Sizes(face_, font_size, font_size);
	// FT_Set_Char_Size(face_, font_size * 64, font_size * 64, 0, 0);
	font_size_ = font_size;
	pthread_mutex_unlock(&g_font_mutex);
	return 0;
}

int get_font_size() { 
    return font_size_; 
}

unsigned int set_font_color(unsigned int font_color) {
	pthread_mutex_lock(&g_font_mutex);
	// argb → bgra
	font_color_ = 0x000000FF;
	font_color_ |= font_color >> 8 & 0x0000FF00;  // R
	font_color_ |= font_color << 8 & 0x00FF0000;  // G
	font_color_ |= font_color << 24 & 0xFF000000; // B
	pthread_mutex_unlock(&g_font_mutex);

	return 0;
}

unsigned int get_font_color( void ) { 
    return font_color_; 
}

void draw_argb8888_buffer(unsigned int *buffer, int buf_w, int buf_h) {
	int i, j, p, q;
	int left = slot_->bitmap_left;
	int top = (face_->size->metrics.ascender >> 6) - slot_->bitmap_top;
	int right = left + slot_->bitmap.width;
	int bottom = top + slot_->bitmap.rows;
	// LOG_DEBUG("top is %d, bottom is %d, left is %d, right is %d\n", top,
	// bottom, left, right);

	for (j = top, q = 0; j < bottom; j++, q++) {
		int offset = j * buf_w;
		int bmp_offset = q * slot_->bitmap.width;
		for (i = left, p = 0; i < right; i++, p++) {
			if (i < 0 || j < 0 || i >= buf_w || j >= buf_h)
				continue;
			// LOG_INFO("bmp_offset + p is %d\n", bmp_offset + p);
			if (slot_->bitmap.buffer[bmp_offset + p]) {
				buffer[offset + i] = font_color_;// printf("0");
			} else {
				buffer[offset + i] = 0x00000000;// printf(".");
			}
			// if (!((i + 1) % slot_->bitmap.width))
			// printf("\n");
		}
	}
}

void draw_argb8888_wchar(unsigned char *buffer, int buf_w, int buf_h, const wchar_t wch) {
	if (!face_) {
		LOG_INFO("please check font_path %s\n", *font_path_);
		return;
	}
	FT_Error error;
	FT_Set_Transform(face_, NULL, &pen_);
	error = FT_Load_Char(face_, wch, FT_LOAD_DEFAULT | FT_LOAD_NO_BITMAP);
	FT_Render_Glyph(slot_, FT_RENDER_MODE_NORMAL); // 8bit per pixel
	if (error) {
		LOG_DEBUG("FT_Load_Char error\n");
		return;
	}
	draw_argb8888_buffer((unsigned int *)buffer, buf_w, buf_h);
}

void draw_argb8888_text(unsigned char *buffer, int buf_w, int buf_h, const wchar_t *wstr) {
	pthread_mutex_lock(&g_font_mutex);
	if (library_ == NULL) {
		LOG_INFO("please check font_path %s\n", *font_path_);
		pthread_mutex_unlock(&g_font_mutex);
		return;
	}
	if (wstr == NULL) {
		LOG_ERROR("wstr is NULL\n");
		pthread_mutex_unlock(&g_font_mutex);
		return;
	}
	int len = wcslen(wstr);
	// wprintf("wstr is %ls\n", wstr);
	// LOG_DEBUG("len is %d\n", len);
	pen_.x = 0;
	// 向上偏移字体高度的1/4，乘以64是因为FreeType使用26.6定点数格式
	pen_.y = (face_->size->metrics.height >> 6) / 4 * 64;
	for (int i = 0; i < len; i++) {
		draw_argb8888_wchar(buffer, buf_w, buf_h, wstr[i]);
		pen_.x += slot_->advance.x;
		pen_.y += slot_->advance.y;
	}
	// save_argb8888_to_bmp(buffer, buf_w, buf_h);
	pthread_mutex_unlock(&g_font_mutex);
}

int wstr_get_actual_advance_x(const wchar_t *wstr) {
	if (wstr == NULL) {
		LOG_ERROR("wstr is NULL\n");
		return -1;
	}
	int len = wcslen(wstr);
	pthread_mutex_lock(&g_font_mutex);
	pen_.x = 0;
	pen_.y = 0;
	if (!face_) {
		LOG_INFO("please check font_path %s\n", *font_path_);
		pthread_mutex_unlock(&g_font_mutex);
		return -1;
	}
	FT_Error error;
	for (int i = 0; i < len; i++) {
		FT_Set_Transform(face_, NULL, &pen_);
		error = FT_Load_Char(face_, wstr[i], FT_LOAD_DEFAULT | FT_LOAD_NO_BITMAP);
		pen_.x += slot_->advance.x;
		pen_.y += slot_->advance.y;
		if( error ){;};
		// LOG_INFO("i is %d, slot_->advance.x is %d, pen_.x is %d\n", i, slot_->advance.x, pen_.x);
	}
	pthread_mutex_unlock(&g_font_mutex);
	return pen_.x / 64; // 26.6 Cartesian pixels, 64 = 2^6
}

int fill_text(osd_data_s *data) {
	if (data->text.font_path == NULL) {
		LOG_ERROR("font_path is NULL\n");
		return -1;
	}
	set_font_color(data->text.font_color);
	draw_argb8888_text(data->buffer, data->width, data->height, data->text.wch);
	return 0;
}

int iconv_utf8_to_wchar(const char *in, wchar_t *out) {
	char entry[128] = {'\0'};
	int ret;
	size_t src_len = strlen(in);
	size_t out_len = MAX_WCH_BYTE;
	char *tmp_out_buffer = (char *)out;
	iconv_t cd = iconv_open("WCHAR_T", "UTF-8");
	if (cd == (iconv_t)-1) {
		perror("iconv_open error");
		return -1;
	}
	memset(entry, 0, 128);
	memcpy(entry, in, src_len); // iconv maybe change the char *
	memset(entry + src_len, 0, 1);
	char *tmp_in_buffer = (char *)entry;
	ret = iconv(cd, &tmp_in_buffer, (size_t *)&src_len, &tmp_out_buffer, (size_t *)&out_len);
	if (ret == -1)
		perror("iconv error");
	iconv_close(cd);
	out[abs((int)(MAX_WCH_BYTE - out_len)) / 4] = '\0';
	return 0;
}

int generate_date_time(const char *fmt, wchar_t *result) {
	char year[8] = {0}, month[4] = {0}, day[4] = {0};
	char week[16] = {0}, hms[12] = {0};
	char ymd_string[32] = {0};
	char week_string[17] = {0};
	char time_string[MAX_WCH_BYTE] = {0};
	int wid = -1;

	time_t curtime;
	curtime = time(0);
	strftime(year, sizeof(year), "%Y", localtime(&curtime));
	strftime(month, sizeof(month), "%m", localtime(&curtime));
	strftime(day, sizeof(day), "%d", localtime(&curtime));

	if (strstr(fmt, OSD_FMT_TIME0)) 
		strftime(hms, sizeof(hms), "%H:%M:%S", localtime(&curtime));
	else if (strstr(fmt, OSD_FMT_TIME1)) 
		strftime(hms, sizeof(hms), "%I:%M:%S %p", localtime(&curtime));
	
	if (strstr(fmt, OSD_FMT_WEEK0)) {
		strftime(week, sizeof(week), "%u", localtime(&curtime));
		wid = week[0] - '0';
		switch (wid) {
		case 1:sprintf(week_string, " 星期一");break;
		case 2:sprintf(week_string, " 星期二");break;
		case 3:sprintf(week_string, " 星期三");break;
		case 4:sprintf(week_string, " 星期四");break;
		case 5:sprintf(week_string, " 星期五");break;
		case 6:sprintf(week_string, " 星期六");break;
		case 7:sprintf(week_string, " 星期日");break;
		default:LOG_ERROR("osd strftime week error\n");sprintf(week_string, " 星期*");break;
		}
	} else if (strstr(fmt, OSD_FMT_WEEK1)) {
		strftime(week, sizeof(week), "%A", localtime(&curtime));
		sprintf(week_string, " %s", week);
	}

	if (strstr(fmt, OSD_FMT_CHR)) {
		if (strstr(fmt, OSD_FMT_YMD0))
			sprintf(ymd_string, "%s年%s月%s日", year, month, day);
		else if (strstr(fmt, OSD_FMT_YMD1))
			sprintf(ymd_string, "%s月%s日%s年", month, day, year);
		else if (strstr(fmt, OSD_FMT_YMD2))
			sprintf(ymd_string, "%s日%s月%s年", day, month, year);
		// Because of the problem with the ipcweb-ng, the string contains CHR,
		// although there is no Chinese in it, it is also included in this judgment
		else if (strstr(fmt, OSD_FMT_YMD3))
			sprintf(ymd_string, "%s/%s/%s", year, month, day);
		else if (strstr(fmt, OSD_FMT_YMD4))
			sprintf(ymd_string, "%s/%s/%s", month, day, year);
		else if (strstr(fmt, OSD_FMT_YMD5))
			sprintf(ymd_string, "%s/%s/%s", day, month, year);
	} else {
		if (strstr(fmt, OSD_FMT_YMD0))
			sprintf(ymd_string, "%s-%s-%s", year, month, day);
		else if (strstr(fmt, OSD_FMT_YMD1))
			sprintf(ymd_string, "%s-%s-%s", month, day, year);
		else if (strstr(fmt, OSD_FMT_YMD2))
			sprintf(ymd_string, "%s-%s-%s", day, month, year);
	}

	snprintf(time_string, MAX_WCH_BYTE, "%s%s %s", ymd_string, week_string, hms);
	//if( curtime % 60 == 0 )
	//	LOG_INFO("time_string is %s\n", time_string);
	iconv_utf8_to_wchar(time_string, result);

	return 0;
}

int rkipc_osd_bmp_create(int id, osd_data_s *osd_data) {
	LOG_INFO("id is %d\n", id);
	int ret = 0;
	RGN_HANDLE RgnHandle = id;
	RGN_ATTR_S stRgnAttr;
	MPP_CHN_S stMppChn;
	RGN_CHN_ATTR_S stRgnChnAttr;
	BITMAP_S stBitmap;

	// create overlay regions
	memset(&stRgnAttr, 0, sizeof(stRgnAttr));
	stRgnAttr.enType = OVERLAY_RGN;
	stRgnAttr.unAttr.stOverlay.enPixelFmt = RK_FMT_ARGB8888;
	stRgnAttr.unAttr.stOverlay.stSize.u32Width = osd_data->width;
	stRgnAttr.unAttr.stOverlay.stSize.u32Height = osd_data->height;
	ret = RK_MPI_RGN_Create(RgnHandle, &stRgnAttr);
	if (RK_SUCCESS != ret) {
		LOG_ERROR("RK_MPI_RGN_Create (%d) failed with %#x\n", RgnHandle, ret);
		RK_MPI_RGN_Destroy(RgnHandle);
		return RK_FAILURE;
	}
	LOG_INFO("The handle: %d, create success\n", RgnHandle);

	// display overlay regions to venc groups
	memset(&stRgnChnAttr, 0, sizeof(stRgnChnAttr));
	stRgnChnAttr.bShow = (RK_BOOL)osd_data->enable;
	stRgnChnAttr.enType = OVERLAY_RGN;
	stRgnChnAttr.unChnAttr.stOverlayChn.stPoint.s32X = osd_data->origin_x;
	stRgnChnAttr.unChnAttr.stOverlayChn.stPoint.s32Y = osd_data->origin_y;
	stRgnChnAttr.unChnAttr.stOverlayChn.u32BgAlpha = 128;
	stRgnChnAttr.unChnAttr.stOverlayChn.u32FgAlpha = 128;
	stRgnChnAttr.unChnAttr.stOverlayChn.u32Layer = id;

	stMppChn.enModId = RK_ID_VENC;
	stMppChn.s32DevId = 0;
	stMppChn.s32ChnId = OSD_MPP_CHN;
	ret = RK_MPI_RGN_AttachToChn(RgnHandle, &stMppChn, &stRgnChnAttr);
	if (RK_SUCCESS != ret) {
		LOG_ERROR("RK_MPI_RGN_AttachToChn (%d) to venc0 failed with %#x\n", RgnHandle, ret);
		return RK_FAILURE;
	}
	LOG_DEBUG("RK_MPI_RGN_AttachToChn to venc0 success\n");
	
	// set bitmap
	stBitmap.enPixelFormat = RK_FMT_ARGB8888;
	stBitmap.u32Width = osd_data->width;
	stBitmap.u32Height = osd_data->height;
	stBitmap.pData = (RK_VOID *)osd_data->buffer;
	ret = RK_MPI_RGN_SetBitMap(RgnHandle, &stBitmap);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("RK_MPI_RGN_SetBitMap failed with %#x\n", ret);
		return RK_FAILURE;
	}

	return ret;
}

int rkipc_osd_bmp_change(int id, osd_data_s *osd_data) {
	int ret = 0;
	RGN_HANDLE RgnHandle = id;
	BITMAP_S stBitmap;
	// set bitmap
	stBitmap.enPixelFormat = RK_FMT_ARGB8888;
	stBitmap.u32Width = osd_data->width;
	stBitmap.u32Height = osd_data->height;
	stBitmap.pData = (RK_VOID *)osd_data->buffer;
	ret = RK_MPI_RGN_SetBitMap(RgnHandle, &stBitmap);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("RK_MPI_RGN_SetBitMap failed with %#x\n", ret);
		return RK_FAILURE;
	}
	return ret;
}

static void *osd_time_server(void *arg) {
	printf("#Start %s thread, arg:%p\n", __func__, arg);
	prctl(PR_SET_NAME, "osd_time_server", 0, 0, 0);
	int osd_time_id = 0;
	int last_time_sec;
	//const char *osd_type ="dateTime";
	const char *date_style;
	const char *time_style;
	osd_data_s osd_data;
	time_t rawtime;
	struct tm *cur_time_info;

	memset(&osd_data, 0, sizeof(osd_data));

	// init
	osd_data.enable = 1;
	osd_data.origin_x = 16;
	osd_data.origin_y = 16;
	osd_data.text.font_size = 32;
	osd_data.text.font_color = 0xfff799;
	osd_data.text.color_inverse = 1;
	osd_data.text.font_path = osdFontPath;

	// get time
	memset(osd_data.text.format, 0, 128);
	date_style = "CHR-YYYY-MM-DD";
	time_style = "24hour";
	int display_week_enabled = 1;
	if (time_style) {
		if (!strcmp(time_style, "12hour")) {
			strcat(osd_data.text.format, OSD_FMT_TIME1);
			strcat(osd_data.text.format, OSD_FMT_SPACE);
		} else {
			strcat(osd_data.text.format, OSD_FMT_TIME0);
			strcat(osd_data.text.format, OSD_FMT_SPACE);
		}
	}
	if (display_week_enabled) {
		strcat(osd_data.text.format, OSD_FMT_WEEK0);
		strcat(osd_data.text.format, OSD_FMT_SPACE);
	}
	if (date_style) 
		strcat(osd_data.text.format, date_style);
	LOG_INFO("osd_data.text.format is %s\n", osd_data.text.format);

	generate_date_time(osd_data.text.format, osd_data.text.wch);
	osd_data.width = UPALIGNTO16(wstr_get_actual_advance_x(osd_data.text.wch));
	osd_data.height = UPALIGNTO16(osd_data.text.font_size);
	osd_data.size = osd_data.width * osd_data.height * 4; // BGRA8888 4byte
	osd_data.buffer = (unsigned char*)malloc(osd_data.size);
	memset(osd_data.buffer, 0, osd_data.size);
	fill_text(&osd_data);
	// rk_osd_bmp_destroy_(osd_time_id);
	if(rkipc_osd_bmp_create(osd_time_id, &osd_data)) {
		LOG_ERROR("rk_osd_bmp_create_ fail\n");
		free(osd_data.buffer);
		return NULL;
	}
	free(osd_data.buffer);

	time(&rawtime);
	cur_time_info = localtime(&rawtime);
	last_time_sec = cur_time_info->tm_sec;
	while ( !quit ) {
		// only update time bmp
		usleep(1000 * 100);
		if( quit ) break;
		time(&rawtime);
		cur_time_info = localtime(&rawtime);
		if (cur_time_info->tm_sec == last_time_sec)
			continue;
		else
			last_time_sec = cur_time_info->tm_sec;
		generate_date_time(osd_data.text.format, osd_data.text.wch);
		osd_data.width = UPALIGNTO16(wstr_get_actual_advance_x(osd_data.text.wch));
		osd_data.height = UPALIGNTO16(osd_data.text.font_size);
		osd_data.size = osd_data.width * osd_data.height * 4; // BGRA8888 4byte
		osd_data.buffer = (unsigned char*)malloc(osd_data.size);
		memset(osd_data.buffer, 0, osd_data.size);
		fill_text(&osd_data);
		rkipc_osd_bmp_change(osd_time_id, &osd_data);
		free(osd_data.buffer);
	}
	LOG_INFO("osd exit\n");

	RK_MPI_RGN_DetachFromChn( osd_time_id, OSD_MPP_CHN );
	RK_MPI_RGN_Destroy( osd_time_id );

	return NULL;
}

int init_osd () {
	LOG_DEBUG("%s\n", __func__);
	osd_data_s osd_data;
	osd_data.text.font_size = 32;
	osd_data.text.font_color = OSD_TIME_COLOR ;
	osd_data.text.font_path = osdFontPath;
	create_font(osd_data.text.font_path, osd_data.text.font_size);
	set_font_color(osd_data.text.font_color);
	pthread_t osd_time_thread_id;
	pthread_create(&osd_time_thread_id, NULL, osd_time_server, NULL);
	pthread_detach(osd_time_thread_id );
	return 0;
}

int deinit_osd() {
	destroy_font();
	LOG_DEBUG("osd deinit over\n");
	return 0;
}



/******************************************************************************** */
int osd_border_create(int id, border_data_s *osd_data) {

	LOG_INFO("border id is %d\n", id);
	int ret = 0;
	RGN_HANDLE RgnHandle = id +1;
	RGN_ATTR_S stRgnAttr;
	MPP_CHN_S stMppChn;
	RGN_CHN_ATTR_S stRgnChnAttr;

	// create overlay regions
	memset(&stRgnAttr, 0, sizeof(stRgnAttr));
	stRgnAttr.enType = OVERLAY_RGN;
	stRgnAttr.unAttr.stOverlay.enPixelFmt = RK_FMT_ARGB8888;
	stRgnAttr.unAttr.stOverlay.stSize.u32Width = osd_data->width;
	stRgnAttr.unAttr.stOverlay.stSize.u32Height = osd_data->height;
	ret = RK_MPI_RGN_Create(RgnHandle, &stRgnAttr);
	if (RK_SUCCESS != ret) {
		LOG_ERROR("RK_MPI_RGN_Create (%d) failed with %#x\n", RgnHandle, ret);
		RK_MPI_RGN_Destroy(RgnHandle);
		return RK_FAILURE;
	}
	LOG_INFO("The border RGN handle: %d, create success\n", RgnHandle);

	// display overlay regions to venc groups
	memset(&stRgnChnAttr, 0, sizeof(stRgnChnAttr));
	stRgnChnAttr.bShow = (RK_BOOL)osd_data->enable;
	stRgnChnAttr.enType = OVERLAY_RGN;
	stRgnChnAttr.unChnAttr.stOverlayChn.stPoint.s32X = osd_data->origin_x;
	stRgnChnAttr.unChnAttr.stOverlayChn.stPoint.s32Y = osd_data->origin_y;
	stRgnChnAttr.unChnAttr.stOverlayChn.u32BgAlpha = 128;
	stRgnChnAttr.unChnAttr.stOverlayChn.u32FgAlpha = 128;
	stRgnChnAttr.unChnAttr.stOverlayChn.u32Layer = id;
	//attach to venc chn 0
	stMppChn.enModId = RK_ID_VENC;
	stMppChn.s32DevId = 0;
	stMppChn.s32ChnId = OSD_MPP_CHN;
	ret = RK_MPI_RGN_AttachToChn(RgnHandle, &stMppChn, &stRgnChnAttr);
	if (RK_SUCCESS != ret) {
		LOG_ERROR("RK_MPI_RGN_AttachToChn (%d) to venc0 failed with %#x\n", RgnHandle, ret);
		return RK_FAILURE;
	}
	LOG_DEBUG("RK_MPI_RGN_AttachToChn to venc0 success\n");
	
	return ret;
}

int fill_border(border_data_s *data) {
	BorderInfo border_info;
 	border_info.rect.w = data->width;
 	border_info.rect.h = data->height;
 	border_info.thick = 3;
	border_info.display_style = BORDER_LINE;
	//border_info.interval = 5;
	border_info.color = data->color;
	draw_border((uint32_t *)data->buffer, border_info);

	return 0;
}

// 增加 canvas_w 参数，用于计算二维坐标在连续内存中的位置
void canvas_draw_solid_border( uint32_t* buffer, 
	int rect_x, int rect_y, int rect_w, int rect_h, 
	int thick, uint32_t color, int canvas_w) {

    // 定位到矩形左上角 (x, y) 的起始指针
    uint32_t *start_pos = buffer + (rect_y * canvas_w + rect_x);

    // 绘制上下两条横线
    for (int j = 0; j < thick; j++) {
        for (int i = 0; i < rect_w; i++) // 上边框：每一行都从 start_pos 开始偏移
            *(start_pos + j * canvas_w + i) = color;
        for (int i = 0; i < rect_w; i++) // 下边框：从起始点向下偏移 rect_h - j - 1 行
            *(start_pos + (rect_h - j - 1) * canvas_w + i) = color;
    }

    // 绘制左右两条竖线
    for (int j = 0; j < rect_h; j++) {
        for (int k = 0; k < thick; k++) {
            *(start_pos + j * canvas_w + k) = color;// 左边框
            *(start_pos + j * canvas_w + (rect_w - thick + k)) = color;// 右边框
        }
    }
}

void canvas_draw_solid_border_new(uint32_t *buffer, 
	int start_x, int start_y, int rect_w, int rect_h, 
	int thick, uint32_t color, int canvas_w ) {

    // 绘制上下边框
    for (int j = 0; j < thick; j++) {
        uint32_t *top_line = buffer + (start_y + j) * canvas_w + start_x;
        uint32_t *bottom_line = buffer + (start_y + rect_h - 1 - j) * canvas_w + start_x;
        for (int i = 0; i < rect_w; i++) {
            top_line[i] = color;
            bottom_line[i] = color;
        }
    }

    // 绘制左右边框
    for (int j = 0; j < rect_h; j++) {
        for (int k = 0; k < thick; k++) {
            buffer[(start_y + j) * canvas_w + start_x + k] = color;                   // 左边框
            buffer[(start_y + j) * canvas_w + start_x + rect_w - 1 - k] = color;     // 右边框
        }
    }
}


int osd_border_change(int id, border_data_s *osd_data) {
	int ret = 0;
	RGN_HANDLE RgnHandle = id +1;
	MPP_CHN_S stMppChn;
	stMppChn.enModId = RK_ID_VENC;
	stMppChn.s32DevId = 0;
	stMppChn.s32ChnId = OSD_MPP_CHN;

    ret = RGN_ChangePosition(RgnHandle, &stMppChn, osd_data->origin_x, osd_data->origin_y);
    if (RK_SUCCESS != ret) {
        LOG_ERROR("Change region(%d) position failed with %#x!", RgnHandle, ret);
        return RK_FAILURE;
    }	
	ret = RGN_ShowOrHide(RgnHandle, &stMppChn, (osd_data->enable?RK_TRUE: RK_FALSE) );
	if (RK_SUCCESS != ret ) {
		LOG_ERROR("Region(%d) show failed with %#x!", RgnHandle, ret);
		return RK_FAILURE;
	}

	// set bitmap
	BITMAP_S stBitmap;
	stBitmap.enPixelFormat = RK_FMT_ARGB8888;
	stBitmap.u32Width = osd_data->width;
	stBitmap.u32Height = osd_data->height;
	stBitmap.pData = (RK_VOID *)osd_data->buffer;
	ret = RK_MPI_RGN_SetBitMap(RgnHandle, &stBitmap);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("RK_MPI_RGN_SetBitMap failed with %#x\n", ret);
		return RK_FAILURE;
	}
	return ret;
}

static void *osd_border_server(void *arg) {
	printf("#Start %s thread, arg:%p\n", __func__, arg);
	prctl(PR_SET_NAME, "osd_border_server", 0, 0, 0);
	//
	int osd_border_id = 0;
	border_data_s osd_data;
	memset(&osd_data, 0, sizeof(osd_data));
	// init
	osd_data.enable = 1 ;
	osd_data.origin_x = 0;
	osd_data.origin_y = 0;
	osd_data.width  = UPALIGNTO16(mainStreamWidth);
	osd_data.height = UPALIGNTO16(mainStreamHeight);
	osd_data.color  = OSD_BORDER_COLOR;
	if(osd_border_create( osd_border_id, &osd_data)) {
		LOG_ERROR("rk_osd_border_create_fail\n");
		return NULL;
	}

	struct timeval start_time, stop_time;
	osd_obj_det_info osd_obj_det_info;
	char cleaned = 0;

	while ( !quit ) {
		//
		sem_wait( obj_det_sem );//wait obj det sen
		if( quit ) break;

		if( 0 == obj_det_info.num ){

			if( 1 == cleaned )
				continue;

			osd_data.enable   = 0;	
			osd_data.origin_x = 0;	
			osd_data.origin_y = 0;	
			osd_data.width    = 2;
			osd_data.height   = 2;	
			osd_data.size = osd_data.width * osd_data.height * 4; // BGRA8888 4byte
			osd_data.buffer = NULL;
			osd_data.buffer = (unsigned char*)malloc(osd_data.size);
			if( NULL == osd_data.buffer ){
				LOG_ERROR("osd date buffer malloc error %d\n", osd_data.size);
				continue;
			}
			memset(osd_data.buffer, 0, osd_data.size);				
			osd_border_change( osd_border_id, &osd_data);
			if( osd_data.buffer ){
				free(osd_data.buffer);
				osd_data.buffer = NULL;
			}
			cleaned = 1;
			printf("clean border osd\n");
			continue;
		}
			
		gettimeofday(&start_time, NULL);
		memcpy( (void*)&osd_obj_det_info, (void*)&obj_det_info , sizeof(osd_obj_det_info) );
		cleaned = 0;

		osd_data.enable = 1;	
		osd_data.origin_x = UPALIGNTO2((int)( osd_obj_det_info.x_start * mainStreamWidth  ));	
		osd_data.origin_y = UPALIGNTO2((int)( osd_obj_det_info.y_start * mainStreamHeight ));	
		osd_data.width    = UPALIGNTO2((int)( osd_obj_det_info.w_whole * mainStreamWidth  ));
		osd_data.height   = UPALIGNTO2((int)( osd_obj_det_info.h_whole * mainStreamHeight ));	
		if( osd_data.width  > mainStreamWidth  ) 	osd_data.width =  UPALIGNTO2(mainStreamWidth);
		if( osd_data.height > mainStreamHeight )	osd_data.height = UPALIGNTO2(mainStreamHeight);
		osd_data.size = osd_data.width * osd_data.height * 4; // BGRA8888 4byte
		osd_data.buffer = NULL;
		osd_data.buffer = (unsigned char*)malloc(osd_data.size);
		if( NULL == osd_data.buffer ){
			LOG_ERROR("osd date buffer malloc error %d\n", osd_data.size);
			continue;
		}
		memset(osd_data.buffer, 0, osd_data.size);	

		for( int i = 0; i < osd_obj_det_info.num ;i ++ ){

			if( osd_obj_det_info.obj_det[i].enable ){
				osd_obj_det_info.obj_det[i].enable = 0;
				
				int x = UPALIGNTO2((int)( osd_obj_det_info.obj_det[i].start_x * mainStreamWidth   ));
				int y = UPALIGNTO2((int)( osd_obj_det_info.obj_det[i].start_y * mainStreamHeight  ));
				int w = UPALIGNTO2((int)( osd_obj_det_info.obj_det[i].width   * mainStreamWidth - 2 ));
				int h = UPALIGNTO2((int)( osd_obj_det_info.obj_det[i].hight   * mainStreamHeight- 2 ));

				canvas_draw_solid_border((uint32_t*)osd_data.buffer, 
						x-osd_data.origin_x, y-osd_data.origin_y, w, h, 3,
						osd_obj_det_info.obj_det[i].color, osd_data.width );
#if 0						
				printf( "draw %d @ (%d,%d %dx%d) %.1f 0x%08X\n" , 
						 osd_obj_det_info.obj_det[i].type, 
						x,y,w,h, 
						osd_obj_det_info.obj_det[i].score, 
						osd_obj_det_info.obj_det[i].color );
#endif
			}
		}
		osd_obj_det_info.num = 0;
		osd_border_change( osd_border_id, &osd_data);
		if( osd_data.buffer ){
			free(osd_data.buffer);
			osd_data.buffer = NULL;
		}
		gettimeofday(&stop_time, NULL);
		float inv =  __get_us(stop_time) - __get_us(start_time);
		if( inv > 1000.0f ){
			printf("border osd x:%d y:%d w:%d h:%d once %.1f us\n", 
				osd_data.origin_x, osd_data.origin_y, osd_data.width, osd_data.height,inv );	
		}
	}
	LOG_INFO("osd border exit\n");

	RK_MPI_RGN_DetachFromChn( osd_border_id, OSD_MPP_CHN );
	RK_MPI_RGN_Destroy( osd_border_id );

	return NULL;
}

int init_broder_osd() 
{
    if( ( strlen(pRKnnModelPath) <3 ) && ( strlen( pIVAModelPath)<3) ){
        RK_LOGI("obj det not work,osd broder return\n");
        return -1;  
    }
	LOG_DEBUG("%s\n", __func__);
	pthread_t osd_border_thread_id;
	pthread_create(&osd_border_thread_id, NULL, osd_border_server, NULL);
	pthread_detach(osd_border_thread_id );
	return 0;
}

int deinit_broder_osd() 
{
	return 0;
}


