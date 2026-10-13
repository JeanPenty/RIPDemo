//stamp:0d0beb39f8b60fce
/*<------------------------------------------------------------------------------------------------->*/
/*该文件由uiresbuilder生成，请不要手动修改*/
/*<------------------------------------------------------------------------------------------------->*/
#ifndef _UIRES_H_
#define _UIRES_H_
	struct _UIRES{
		struct _UIDEF{
			const TCHAR * XML_INIT;
			}UIDEF;
		struct _LAYOUT{
			const TCHAR * XML_MAINWND;
			}LAYOUT;
		struct _values{
			const TCHAR * string;
			const TCHAR * color;
			const TCHAR * skin;
			}values;
		struct _PNG{
			const TCHAR * IDB_SHADOW;
			const TCHAR * IDB_SHADOW1;
			const TCHAR * IDB_PNG_ALPHA;
			const TCHAR * IDB_COMMON_BTN60_30;
			const TCHAR * IDB_COMMON_BTN60_30MB;
			const TCHAR * IDB_COMMON_BTN_CLOSE;
			const TCHAR * IDB_COMMON_BTN_MAX;
			const TCHAR * IDB_COMMON_BTN_MENU;
			const TCHAR * IDB_COMMON_BTN_MIN;
			const TCHAR * IDB_COMMON_BTN_RESTORE;
			const TCHAR * IDB_COMMON_BTN_SKIN;
			const TCHAR * IDB_COMMON_SB;
			const TCHAR * IDB_COMMON_VSB;
			const TCHAR * IDB_COMMON_TAB_BG;
			const TCHAR * IDB_COMMON_BTN_CBX;
			const TCHAR * IDB_MENU_ITEMSKIN;
			}PNG;
		struct _ICON{
			const TCHAR * ICON_LOGO;
			}ICON;
		struct _translator{
			const TCHAR * lang_cn;
			const TCHAR * lang_jp;
			}translator;
		struct _SMENU{
			const TCHAR * menu_edit;
			const TCHAR * menu_file;
			const TCHAR * menu_help;
			}SMENU;
	};
#endif//_UIRES_H_
#ifdef INIT_R_DATA
struct _UIRES UIRES={
		{
			_T("UIDEF:XML_INIT"),
		},
		{
			_T("LAYOUT:XML_MAINWND"),
		},
		{
			_T("values:string"),
			_T("values:color"),
			_T("values:skin"),
		},
		{
			_T("PNG:IDB_SHADOW"),
			_T("PNG:IDB_SHADOW1"),
			_T("PNG:IDB_PNG_ALPHA"),
			_T("PNG:IDB_COMMON_BTN60_30"),
			_T("PNG:IDB_COMMON_BTN60_30MB"),
			_T("PNG:IDB_COMMON_BTN_CLOSE"),
			_T("PNG:IDB_COMMON_BTN_MAX"),
			_T("PNG:IDB_COMMON_BTN_MENU"),
			_T("PNG:IDB_COMMON_BTN_MIN"),
			_T("PNG:IDB_COMMON_BTN_RESTORE"),
			_T("PNG:IDB_COMMON_BTN_SKIN"),
			_T("PNG:IDB_COMMON_SB"),
			_T("PNG:IDB_COMMON_VSB"),
			_T("PNG:IDB_COMMON_TAB_BG"),
			_T("PNG:IDB_COMMON_BTN_CBX"),
			_T("PNG:IDB_MENU_ITEMSKIN"),
		},
		{
			_T("ICON:ICON_LOGO"),
		},
		{
			_T("translator:lang_cn"),
			_T("translator:lang_jp"),
		},
		{
			_T("SMENU:menu_edit"),
			_T("SMENU:menu_file"),
			_T("SMENU:menu_help"),
		},
	};
#else
extern struct _UIRES UIRES;
#endif//INIT_R_DATA

#ifndef _R_H_
#define _R_H_
struct _R{
	struct _name{
		 const wchar_t * btn_close;
		 const wchar_t * btn_log_all;
		 const wchar_t * btn_max;
		 const wchar_t * btn_min;
		 const wchar_t * btn_outroot;
		 const wchar_t * btn_restore;
		 const wchar_t * cap_main;
		 const wchar_t * cb_dither;
		 const wchar_t * cb_prn;
		 const wchar_t * cbx_nozzle_type;
		 const wchar_t * check_fsserp;
		 const wchar_t * check_gray2k;
		 const wchar_t * check_noaa;
		 const wchar_t * check_spot;
		 const wchar_t * dot_line_c;
		 const wchar_t * dot_line_k;
		 const wchar_t * dot_line_m;
		 const wchar_t * dot_line_sum;
		 const wchar_t * dot_line_y;
		 const wchar_t * dot_tip;
		 const wchar_t * edit_icc_path;
		 const wchar_t * edit_noaa;
		 const wchar_t * edit_outroot;
		 const wchar_t * edit_threads;
		 const wchar_t * edit_threshold_c;
		 const wchar_t * edit_threshold_k;
		 const wchar_t * edit_threshold_m;
		 const wchar_t * edit_threshold_y;
		 const wchar_t * edit_x_dpi;
		 const wchar_t * edit_y_dpi;
		 const wchar_t * item_name;
		 const wchar_t * lv_rip_task;
		 const wchar_t * menu_about;
		 const wchar_t * menu_file;
		 const wchar_t * menu_file_addfile;
		 const wchar_t * menu_file_clearfile;
		 const wchar_t * menu_file_removefile;
		 const wchar_t * menu_file_riprun;
		 const wchar_t * menu_file_stoprip;
		 const wchar_t * menu_help;
		 const wchar_t * prog_rip;
		 const wchar_t * prog_text;
	}name;
	struct _id{
		int btn_close;
		int btn_log_all;
		int btn_max;
		int btn_min;
		int btn_outroot;
		int btn_restore;
		int cap_main;
		int cb_dither;
		int cb_prn;
		int cbx_nozzle_type;
		int check_fsserp;
		int check_gray2k;
		int check_noaa;
		int check_spot;
		int dot_line_c;
		int dot_line_k;
		int dot_line_m;
		int dot_line_sum;
		int dot_line_y;
		int dot_tip;
		int edit_icc_path;
		int edit_noaa;
		int edit_outroot;
		int edit_threads;
		int edit_threshold_c;
		int edit_threshold_k;
		int edit_threshold_m;
		int edit_threshold_y;
		int edit_x_dpi;
		int edit_y_dpi;
		int item_name;
		int lv_rip_task;
		int menu_about;
		int menu_file;
		int menu_file_addfile;
		int menu_file_clearfile;
		int menu_file_removefile;
		int menu_file_riprun;
		int menu_file_stoprip;
		int menu_help;
		int prog_rip;
		int prog_text;
	}id;
	struct _color{
		int blue;
		int gray;
		int green;
		int red;
		int white;
	}color;
	struct _string{
		int title;
		int ver;
	}string;

};
#endif//_R_H_
#ifdef INIT_R_DATA
struct _R R={
	{
		L"btn_close",
		L"btn_log_all",
		L"btn_max",
		L"btn_min",
		L"btn_outroot",
		L"btn_restore",
		L"cap_main",
		L"cb_dither",
		L"cb_prn",
		L"cbx_nozzle_type",
		L"check_fsserp",
		L"check_gray2k",
		L"check_noaa",
		L"check_spot",
		L"dot_line_c",
		L"dot_line_k",
		L"dot_line_m",
		L"dot_line_sum",
		L"dot_line_y",
		L"dot_tip",
		L"edit_icc_path",
		L"edit_noaa",
		L"edit_outroot",
		L"edit_threads",
		L"edit_threshold_c",
		L"edit_threshold_k",
		L"edit_threshold_m",
		L"edit_threshold_y",
		L"edit_x_dpi",
		L"edit_y_dpi",
		L"item_name",
		L"lv_rip_task",
		L"menu_about",
		L"menu_file",
		L"menu_file_addfile",
		L"menu_file_clearfile",
		L"menu_file_removefile",
		L"menu_file_riprun",
		L"menu_file_stoprip",
		L"menu_help",
		L"prog_rip",
		L"prog_text"
	}
	,
	{
		65538,
		65558,
		65539,
		65541,
		65560,
		65540,
		65536,
		65554,
		65553,
		65546,
		65555,
		65550,
		65552,
		65549,
		65565,
		65568,
		65566,
		65569,
		65567,
		65570,
		65557,
		65551,
		65559,
		65556,
		65561,
		65564,
		65562,
		65563,
		65547,
		65548,
		65543,
		65542,
		102,
		65537,
		1,
		4,
		3,
		2,
		5,
		101,
		65544,
		65545
	}
	,
	{
		0,
		1,
		2,
		3,
		4
	}
	,
	{
		0,
		1
	}
	
};
#else
extern struct _R R;
#endif//INIT_R_DATA
