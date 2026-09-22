#pragma once
class CGlobalUnits
{
public:
	static CGlobalUnits& Instance()
	{
		static CGlobalUnits instance;
		return instance;
	}

	CGlobalUnits(const CGlobalUnits&) = delete;
	CGlobalUnits& operator=(const CGlobalUnits&) = delete;
private:
	CGlobalUnits() = default;
	~CGlobalUnits() = default;


public:
	SStringW m_sstrAppPath = L"";

	cmsHPROFILE m_hCMYKProfile = NULL;	//×ª»»CMYK
	cmsHPROFILE m_hRGBProfile = NULL;
	cmsHTRANSFORM m_hTransformBGRA2CMYK = NULL;
};

