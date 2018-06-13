#include "Unit_Logging.h"

#ifdef _CPPUNIT_TEST

CUnit_Frame_Logging::~CUnit_Frame_Logging()
{

}

void CUnit_Frame_Logging::setUp(void)
{
    m_pFrameLoggingStrategy = new Frame_Logging_Strategy();

    //�Ƿ��ACE_DEBUG�ļ��洢
    Logging_Config_Param objParam;

    sprintf_safe(objParam.m_strLogFile, 256, "servertest.log");
    objParam.m_iChkInterval    = 600;
    objParam.m_iLogFileMaxCnt  = 3;
    objParam.m_iLogFileMaxSize = 10240;
    sprintf_safe(objParam.m_strLogLevel, 128, "INFO");

    m_pFrameLoggingStrategy->InitLogStrategy(objParam);
}

void CUnit_Frame_Logging::tearDown(void)
{
    m_pFrameLoggingStrategy->EndLogStrategy();
    SAFE_DELETE(m_pFrameLoggingStrategy);
}



void CUnit_Frame_Logging::Test_Debug_Log(void)
{
    ACE_DEBUG((LM_INFO, "[CUnit_Frame_Logging]Test_Debug_Log ok.\n"));
}

#endif

