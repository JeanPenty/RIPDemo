//stamp:0d0ac734233578f2
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
		 const wchar_t * btn_min;
		 const wchar_t * cap_main;
		 const wchar_t * menu_about;
		 const wchar_t * menu_help;
		 const wchar_t * split_row;
	}name;
	struct _id{
		int btn_close;
		int btn_min;
		int cap_main;
		int menu_about;
		int menu_help;
		int split_row;
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
		L"btn_min",
		L"cap_main",
		L"menu_about",
		L"menu_help",
		L"split_row"
	}
	,
	{
		65537,
		65538,
		65536,
		102,
		101,
		65539
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
