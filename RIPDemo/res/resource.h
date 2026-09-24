//stamp:0d0b6d61b5351c2a
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
		 const wchar_t * btn_max;
		 const wchar_t * btn_min;
		 const wchar_t * btn_restore;
		 const wchar_t * cap_main;
		 const wchar_t * cbx_nozzle_type;
		 const wchar_t * item_name;
		 const wchar_t * lv_rip_task;
		 const wchar_t * menu_about;
		 const wchar_t * menu_help;
	}name;
	struct _id{
		int btn_close;
		int btn_max;
		int btn_min;
		int btn_restore;
		int cap_main;
		int cbx_nozzle_type;
		int item_name;
		int lv_rip_task;
		int menu_about;
		int menu_help;
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
		L"btn_max",
		L"btn_min",
		L"btn_restore",
		L"cap_main",
		L"cbx_nozzle_type",
		L"item_name",
		L"lv_rip_task",
		L"menu_about",
		L"menu_help"
	}
	,
	{
		65537,
		65538,
		65540,
		65539,
		65536,
		65543,
		65542,
		65541,
		102,
		101
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
