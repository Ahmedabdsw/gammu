// ViewModePanel.cpp: implementation of the CViewModePanel class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "Calendar.h"
#include "ViewModePanel.h"

#ifdef _DEBUG
#undef THIS_FILE
static char THIS_FILE[]=__FILE__;
#define new DEBUG_NEW
#endif

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CViewModePanel::CViewModePanel()
{
#ifdef _CalendarMode_
	UINT nID[VIEWMODE_TBN_NUM] = {IDC_TBN_REPORT,IDC_TBN_CARD,IDC_TBN_ICON,IDC_TBN_LIST};
#else 
	UINT nID[VIEWMODE_TBN_NUM] = {IDC_TBN_REPORT,IDC_TBN_CARD,IDC_TBN_ICON};
#endif
	for(int i = 0 ; i < VIEWMODE_TBN_NUM ; i ++){
		m_nID[i] = nID[i];
		m_pTabBtn[i] = NULL;	
	}
	m_pImgBuffer = NULL;
}

CViewModePanel::~CViewModePanel()
{
}

BEGIN_MESSAGE_MAP(CViewModePanel, CPanel)
	//{{AFX_MSG_MAP(CViewModePanel)
	ON_WM_CREATE()
	ON_WM_PAINT()
	ON_WM_SIZE()
	ON_WM_DESTROY()
	//������Ϣ�ܹ�ʵ��tab��ť�ĵ����Ӧ
	ON_COMMAND(IDC_TBN_REPORT, OnReport)
	ON_COMMAND(IDC_TBN_CARD , OnCard)
	ON_COMMAND(IDC_TBN_ICON , OnIcon)
	ON_COMMAND(IDC_TBN_LIST , OnList)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

void CViewModePanel::OnReport()
{
	xSetTabActive(m_nID[0]);
	::PostMessage(GetParent()->GetSafeHwnd(),WM_VIEWMODE_CHANGE,0,0);
}

void CViewModePanel::OnCard()
{
	xSetTabActive(m_nID[1]);
	::PostMessage(GetParent()->GetSafeHwnd(),WM_VIEWMODE_CHANGE,1,0);
}

void CViewModePanel::OnIcon()
{
	xSetTabActive(m_nID[2]);
	::PostMessage(GetParent()->GetSafeHwnd(),WM_VIEWMODE_CHANGE,2,0);
}

void CViewModePanel::OnList()
{
	xSetTabActive(m_nID[3]);
	::PostMessage(GetParent()->GetSafeHwnd(),WM_VIEWMODE_CHANGE,3,0);
}

void CViewModePanel::OnPaint()
{
	CPaintDC dc(this); // device context for painting


	CRect rect;
	GetClientRect(rect);
	Graphics graph(m_pImgBuffer);

	m_line.DrawLine(&graph, m_rcLine);
	m_pImgBuffer->Display(dc.GetSafeHdc(),rect);

}

int CViewModePanel::OnCreate(LPCREATESTRUCT lpCreateStruct) 
{
	if (CPanel::OnCreate(lpCreateStruct) == -1)
		return -1;

	//get profile path
	Tstring sProfile(theApp.m_szSkin);
	//sProfile += string("Organize\\ViewMode_Calendar.ini");
	sProfile += Tstring(_T("Calendar\\ViewMode_Calendar.ini"));//nono, 2004_1101

	//create phone book button
	m_pTabBtn[0] = &m_tbReport;
	m_pTabBtn[1] = &m_tbCard;
	m_pTabBtn[2] = &m_tbIcon;
	m_pTabBtn[3] = &m_tbList;

#ifdef _CalendarMode_	//��ͷ�ļ��ж���
	//ָ��Ҫ��ȡ��ini��sec key
	TCHAR szSec[VIEWMODE_TBN_NUM][32] = {_T("Day"),_T("Week"),_T("Month"),_T("List")};
	TCHAR  szText[VIEWMODE_TBN_NUM][32] = {_T("IDS_Day"),_T("IDS_Week"),_T("IDS_Month"),_T("IDS_List")};
#else 
	TCHAR szSec[VIEWMODE_TBN_NUM][32] = {_T("report"),_T("card"),_T("icon")};
	TCHAR  szText[VIEWMODE_TBN_NUM][32] = {_T("IDS_REPORT"),_T("IDS_CARD"),_T("IDS_ICON")};
#endif
	//tab��ť�����ﴴ��
	for(int i = 0 ; i < VIEWMODE_TBN_NUM ; i++)
	{
		//Create the tab button
		//sProfile = skin--Calendar\\ViewMode_Calendar.ini
		CRect rc,rect;
		al_GetSettingRect(szSec[i],_T("rect"),const_cast<TCHAR *>(sProfile.c_str()),rect);
		m_pTabBtn[i]->Create(NULL,WS_VISIBLE | BS_OWNERDRAW,rect,this,m_nID[i]);
		//Load the profile setting 
		//��ťͼƬ�ȵ�����ͨ������ĺ���ʵ��
		GetButtonFromSetting(m_pTabBtn[i],szSec[i],szText[i],0,const_cast<TCHAR *>(sProfile.c_str()));
		m_pTabBtn[i]->TransparentBk(TRUE);
	}
	//���溯����������tab��ť��״̬����û�н���������ͼ���л�
	//��ͼ���л���ͨ����Ӧ��ť�ĵ����Ϣ ��������Ϣ����������ʵ�ֵ�
	xSetTabActive(m_nID[0]);	//Ĭ������ǵ�һ����ť�ǻ��

	//m_tbIcon.ShowWindow(SW_HIDE);

	//Load the background image
	//sProfile = skin--Calendar\\ViewMode_Calendar.ini
	TCHAR szImg[MAX_PATH];
	COLORREF color;
	//������뵼�뱳��ͼƬ����������ھ͵��뱳����ɫ
	//ע�⣺ ͼƬ����ɫ���������ڵ� ����tab��ť��
	if(al_GetSettingString(_T("panel"),_T("Bg_Img"),const_cast<TCHAR *>(sProfile.c_str()),szImg))
	{
		Tstring sImg(theApp.m_szSkin);
		sImg += szImg;

		CImage ImgBg;
		if(ImgBg.LoadFile(sImg.c_str()) == Ok)
		{
			//draw the image buffer 
			CRect rect;
			GetWindowRect(&rect);
			int iWidth = ::GetSystemMetrics(SM_CXFULLSCREEN);
			m_pImgBuffer = new CImage(iWidth,rect.Height());
			Graphics graph(m_pImgBuffer);
			RECT rc = {0,0,iWidth,rect.Height()};
			ImgBg.Display(graph,&rc,DRAW_TILE);
		}
	}
	else if(al_GetSettingColor(_T("panel"),_T("brush"),const_cast<TCHAR *>(sProfile.c_str()),color))
	{
		CRect rect;
		GetWindowRect(&rect);
		int iWidth = ::GetSystemMetrics(SM_CXFULLSCREEN);
		CImage ImgBg(iWidth,rect.Height());
		Graphics grap(&ImgBg);
		Color cr;
		cr.SetFromCOLORREF(color);
		SolidBrush brush(cr);
		Rect Rc(0,0,ImgBg.Width(),ImgBg.Height());
		grap.FillRectangle(&brush,Rc);
		m_pImgBuffer = new CImage(iWidth,rect.Height());
		Graphics graph(m_pImgBuffer);
		RECT rc = {0,0,iWidth,rect.Height()};
		ImgBg.Display(graph,&rc,DRAW_TILE);
	}
	/* m_line�ı���
		int m_wFrame;
		COLORREF m_crFrame;
		COLORREF m_crShadow;
		CSize m_ellip;
		CSize m_off;
	*/
	//m_line�����ﱻ���ã���
	//�˴���û���õ���һ����
	if(al_GetSettingRect(_T("line"), _T("resize_rect"), const_cast<TCHAR *>(sProfile.c_str()), m_rcLine))
	{	
		al_GetSettingColor(_T("line"), _T("line_shadow"), const_cast<TCHAR *>(sProfile.c_str()), m_line.m_crShadow);
		al_GetSettingColor(_T("line"), _T("line_color"), const_cast<TCHAR *>(sProfile.c_str()), m_line.m_crFrame);
		al_GetSettingInt(_T("line"), _T("line_width"), const_cast<TCHAR *>(sProfile.c_str()), m_line.m_wFrame);
		al_GetSettingSize(_T("line"), _T("line_round"), const_cast<TCHAR *>(sProfile.c_str()), m_line.m_ellip);
		al_GetSettingSize(_T("line"), _T("line_shift"), const_cast<TCHAR *>(sProfile.c_str()), m_line.m_off);
	}

	return 0;
}

void CViewModePanel::xSetTabActive(UINT nID)
{
	for(int i = 0; i < VIEWMODE_TBN_NUM; i ++)
	{
		if(nID == m_nID[i])
		{
			m_pTabBtn[i]->SetCheck(TRUE);
			m_pTabBtn[i]->BringWindowToTop();
		}
		else
		{
			m_pTabBtn[i]->SetCheck(FALSE);
		}
	}
}

void CViewModePanel::OnSize(UINT nType, int cx, int cy) 
{
	CPanel::OnSize(nType, cx, cy);
	
	if(m_pTabBtn[0]){
		CRect rect;
		GetClientRect(rect);
		for(int i = 1 ; i < VIEWMODE_TBN_NUM ; i ++){
			CRect rcPrev,rc;
			m_pTabBtn[i-1]->GetWindowRect(rcPrev);
			ScreenToClient(&rcPrev);
			m_pTabBtn[i]->GetWindowRect(rc);
			ScreenToClient(&rc);
			rc.OffsetRect(rcPrev.right - rc.left,0);
			m_pTabBtn[i]->MoveWindow(rc);
		}
	}
	m_rcLine.SetRect(0,0,cx,0);
}

void CViewModePanel::OnDestroy() 
{
	CPanel::OnDestroy();
	
	SAFE_DELPTR(m_pImgBuffer);
}

void CViewModePanel::EnableDailyWeeklyMonthlyView(BOOL bEnable)
{
	for(int  i = 0 ; i < VIEWMODE_TBN_NUM -1; i ++)
	{
		m_pTabBtn[i]->EnableWindow(bEnable);
	}

}
