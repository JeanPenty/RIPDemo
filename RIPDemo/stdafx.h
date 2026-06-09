// stdafx.h : include file for standard system include files,
//  or project specific include files that are used frequently, but
//  are changed infrequently
//

#pragma once

#define  _CRT_SECURE_NO_WARNINGS
#define	 DLL_SOUI
#include <souistd.h>
#include <core/SHostDialog.h>
#include <control/SMessageBox.h>
#include <control/SouiCtrls.h>
#include <res.mgr/SObjDefAttr.h>
#include <com-cfg.h>
#include <SouiFactory.h>
#include "resource.h"
using namespace SNS;

//mupdf
#include "mupdf/fitz.h"
#include "mupdf/pdf.h"
#include "mupdf/fitz/color.h"

//tiff
#include "tiffio.h"

//lcms
#include "lcms2.h"

#include <string>