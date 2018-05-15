#include "ProServerManager.h"
#include "Frame_Logging_Strategy.h"

CProServerManager::CProServerManager(void)
{
    m_pFrameLoggingStrategy = NULL;
}

CProServerManager::~CProServerManager(void)
{
}

bool CProServerManager::Init()
{
    //是否打开ACE_DEBUG文件存储
    Server_Manager_Common_FrameLogging(m_pFrameLoggingStrategy);

    int nServerPortCount    = App_MainConfig::instance()->GetServerPortCount();
    int nReactorCount       = App_MainConfig::instance()->GetReactorCount();

    bool blState = false;

    //初始化模块数组相关参数
    App_MessageManager::instance()->Init(App_MainConfig::instance()->GetMaxModuleCount(), App_MainConfig::instance()->GetMaxCommandCount());

    //初始化加载模块的信息
    App_ModuleLoader::instance()->Init(App_MainConfig::instance()->GetMaxModuleCount());

    //初始化禁止IP列表
    App_ForbiddenIP::instance()->Init(FORBIDDENIP_FILE);

    //初始化日志系统线程
    if (false == Server_Manager_Common_LogSystem())
    {
        return false;
    }

    //初始化各种对象池
    Server_Manager_Common_Pool();

    //初始化ProConnectHandler对象池
    if(App_MainConfig::instance()->GetMaxHandlerCount() <= 0)
    {
        //初始化PacketParse对象池
        App_ProConnectHandlerPool::instance()->Init(MAX_HANDLE_POOL);
    }
    else
    {
        //初始化PacketParse对象池
        App_ProConnectHandlerPool::instance()->Init(App_MainConfig::instance()->GetMaxHandlerCount());
    }

    //初始化链接管理器
    App_ProConnectManager::instance()->Init(App_MainConfig::instance()->GetSendQueueCount());

    //初始化给插件的对象接口
    IConnectManager* pConnectManager       = dynamic_cast<IConnectManager*>(App_ProConnectManager::instance());
    IClientManager*  pClientManager        = dynamic_cast<IClientManager*>(App_ClientProConnectManager::instance());
    IUDPConnectManager* pUDPConnectManager = dynamic_cast<IUDPConnectManager*>(App_ProUDPManager::instance());
    IFrameCommand* pFrameCommand           = dynamic_cast<IFrameCommand*>(&m_objFrameCommand);
    IServerManager* pServerManager         = dynamic_cast<IServerManager*>(this);
    Server_Manager_Common_IObject(pConnectManager,
                                  pClientManager,
                                  pUDPConnectManager,
                                  pFrameCommand,
                                  pServerManager);

    //初始化消息处理线程
    App_MessageServiceGroup::instance()->Init(App_MainConfig::instance()->GetThreadCount(),
            App_MainConfig::instance()->GetMsgMaxQueue(),
            App_MainConfig::instance()->GetMsgLowMark(),
            App_MainConfig::instance()->GetMgsHighMark());

    //初始化模块加载，因为这里可能包含了中间服务器连接加载
    if (false == Server_Manager_Common_Module())
    {
        return false;
    }

    //让所有的线程拷同步副本
    App_MessageServiceGroup::instance()->CopyMessageManagerList();

    //初始化连接器
    uint32 u4ClientProactorCount = (uint32)nReactorCount - 3;

    if (!App_ProConnectAcceptManager::instance()->InitConnectAcceptor(nServerPortCount, u4ClientProactorCount))
    {
        OUR_DEBUG((LM_INFO, "[CProServerManager::Init]%s.\n", App_ProConnectAcceptManager::instance()->GetError()));
        return false;
    }

    //初始化反应器集合
    App_ProactorManager::instance()->Init((uint16)nReactorCount);

    //初始化反应器
    for (int i = 0; i < nReactorCount; i++)
    {
        OUR_DEBUG((LM_INFO, "[CProServerManager::Init()]... i=[%d].\n", i));

        if (App_MainConfig::instance()->GetNetworkMode() == NETWORKMODE_PRO_IOCP)
        {
            blState = App_ProactorManager::instance()->AddNewProactor(i, Proactor_WIN32, 1);
            OUR_DEBUG((LM_INFO, "[CProServerManager::Init]AddNewProactor NETWORKMODE = Proactor_WIN32.\n"));
        }
        else
        {
            OUR_DEBUG((LM_INFO, "[CProServerManager::Init]AddNewProactor NETWORKMODE Error.\n"));
            return false;
        }

        if (!blState)
        {
            OUR_DEBUG((LM_INFO, "[CProServerManager::Init]AddNewProactor [%d] Error.\n", i));
            return false;
        }
    }

    return true;
}

bool CProServerManager::Start()
{
    //启动TCP监听
    int nServerPortCount = App_MainConfig::instance()->GetServerPortCount();

    //初始化监听远程连接
    for(int i = 0 ; i < nServerPortCount; i++)
    {
        ACE_INET_Addr listenAddr;

        _ServerInfo* pServerInfo = App_MainConfig::instance()->GetServerPort(i);

        if (false == Server_Manager_Common_Addr(pServerInfo, listenAddr))
        {
            return false;
        }

        //得到接收器
        ProConnectAcceptor* pConnectAcceptor = App_ProConnectAcceptManager::instance()->GetConnectAcceptor(i);

        if(NULL == pConnectAcceptor)
        {
            OUR_DEBUG((LM_INFO, "[CProServerManager::Start]pConnectAcceptor[%d] is NULL.\n", i));
            return false;
        }

        //设置监听IP信息
        pConnectAcceptor->SetPacketParseInfoID(pServerInfo->m_u4PacketParseInfoID);
        pConnectAcceptor->SetListenInfo(pServerInfo->m_szServerIP, (uint32)pServerInfo->m_nPort);

        ACE_Proactor* pProactor = App_ProactorManager::instance()->GetAce_Proactor(REACTOR_CLIENTDEFINE);

        if(NULL == pProactor)
        {
            OUR_DEBUG((LM_INFO, "[CProServerManager::Start]App_ProactorManager::instance()->GetAce_Proactor(REACTOR_CLIENTDEFINE) is NULL.\n"));
            return false;
        }

        int nRet = pConnectAcceptor->open(listenAddr, 0, 1, App_MainConfig::instance()->GetBacklog(), 1, pProactor);

        if(-1 == nRet)
        {
            OUR_DEBUG((LM_INFO, "[CProServerManager::Start] pConnectAcceptor->open[%d] is error.\n", i));
            OUR_DEBUG((LM_INFO, "[CProServerManager::Start] Listen from [%s:%d] error(%d).\n",listenAddr.get_host_addr(), listenAddr.get_port_number(), errno));
            return false;
        }

        OUR_DEBUG((LM_INFO, "[CProServerManager::Start] Listen from [%s:%d] OK.\n", listenAddr.get_host_addr(), listenAddr.get_port_number()));
    }

    //启动UDP监听
    int nUDPServerPortCount = App_MainConfig::instance()->GetUDPServerPortCount();

    for(int i = 0 ; i < nUDPServerPortCount; i++)
    {
        ACE_INET_Addr listenAddr;

        _ServerInfo* pServerInfo = App_MainConfig::instance()->GetUDPServerPort(i);

        CProactorUDPHandler* pProactorUDPHandler = App_ProUDPManager::instance()->Create();

        if(NULL == pProactorUDPHandler)
        {
            OUR_DEBUG((LM_INFO, "[CProServerManager::Start] pProactorUDPHandler is NULL[%d] is error.\n", i));
            return false;
        }
        else
        {
            pProactorUDPHandler->SetPacketParseInfoID(pServerInfo->m_u4PacketParseInfoID);
            int nErr = 0;

            if (false == Server_Manager_Common_Addr(pServerInfo, listenAddr))
            {
                return false;
            }

            ACE_Proactor* pProactor = App_ProactorManager::instance()->GetAce_Proactor(REACTOR_CLIENTDEFINE);

            if(NULL == pProactor)
            {
                OUR_DEBUG((LM_INFO, "[CProServerManager::Start]UDP App_ProactorManager::instance()->GetAce_Proactor(REACTOR_CLIENTDEFINE) is NULL.\n"));
                return false;
            }

            if(0 != pProactorUDPHandler->OpenAddress(listenAddr, pProactor))
            {
                OUR_DEBUG((LM_INFO, "[CProServerManager::Start] UDP Listen from [%s:%d] error(%d).\n",listenAddr.get_host_addr(), listenAddr.get_port_number(), errno));
                return false;
            }

            OUR_DEBUG((LM_INFO, "[CProServerManager::Start] UDP Listen from [%s:%d] OK.\n", listenAddr.get_host_addr(), listenAddr.get_port_number()));
        }
    }

    //启动后台管理端口监听
    if(App_MainConfig::instance()->GetConsoleSupport() == CONSOLE_ENABLE)
    {
        ACE_INET_Addr listenConsoleAddr;

        int nErr = 0;

        if(App_MainConfig::instance()->GetConsoleIPType() == TYPE_IPV4)
        {
            if(ACE_OS::strcmp(App_MainConfig::instance()->GetConsoleIP(), "INADDR_ANY") == 0)
            {
                nErr = listenConsoleAddr.set(App_MainConfig::instance()->GetConsolePort(),
                                             (uint32)INADDR_ANY);
            }
            else
            {
                nErr = listenConsoleAddr.set(App_MainConfig::instance()->GetConsolePort(),
                                             App_MainConfig::instance()->GetConsoleIP());
            }
        }
        else
        {
            if(ACE_OS::strcmp(App_MainConfig::instance()->GetConsoleIP(), "INADDR_ANY") == 0)
            {
                nErr = listenConsoleAddr.set(App_MainConfig::instance()->GetConsolePort(),
                                             (uint32)INADDR_ANY);
            }
            else
            {
                nErr = listenConsoleAddr.set(App_MainConfig::instance()->GetConsolePort(),
                                             App_MainConfig::instance()->GetConsoleIP(), 1, PF_INET6);
            }
        }

        if(nErr != 0)
        {
            OUR_DEBUG((LM_INFO, "[CProServerManager::Start]listenConsoleAddr set_address error[%d].\n", errno));
            return false;
        }

        ACE_Proactor* pProactor = App_ProactorManager::instance()->GetAce_Proactor(REACTOR_CLIENTDEFINE);

        if(NULL == pProactor)
        {
            OUR_DEBUG((LM_INFO, "[CProServerManager::Start]App_ProactorManager::instance()->GetAce_Proactor(REACTOR_CLIENTDEFINE) is NULL.\n"));
            return false;
        }

        int nRet = m_ProConsoleConnectAcceptor.open(listenConsoleAddr, 0, 1, MAX_ASYNCH_BACKLOG, 1, pProactor, true);

        if(-1 == nRet)
        {
            OUR_DEBUG((LM_INFO, "[CProServerManager::Start] m_ProConsoleConnectAcceptor.open is error.\n"));
            OUR_DEBUG((LM_INFO, "[CProServerManager::Start] Listen from [%s:%d] error(%d).\n",listenConsoleAddr.get_host_addr(), listenConsoleAddr.get_port_number(), errno));
            return false;
        }
    }

    //启动日志服务线程
    if(0 != AppLogManager::instance()->Start())
    {
        AppLogManager::instance()->WriteLog(LOG_SYSTEM, "[CProServerManager::Init]AppLogManager is ERROR.");
    }
    else
    {
        AppLogManager::instance()->WriteLog(LOG_SYSTEM, "[CProServerManager::Init]AppLogManager is OK.");
    }

    //启动定时器
    if(0 != App_TimerManager::instance()->activate())
    {
        OUR_DEBUG((LM_INFO, "[CProServerManager::Start]App_TimerManager::instance()->Start() is error.\n"));
        return false;
    }

    //启动反应器(其他的反应器，因为插件第三方需要)
    if(!App_ProactorManager::instance()->StartOtherProactor())
    {
        OUR_DEBUG((LM_INFO, "[CProServerManager::Start]App_ProactorManager::instance()->StartOtherProactor is error.\n"));
        return false;
    }

    //初始化服务器间通讯类
    App_ClientProConnectManager::instance()->Init(App_ProactorManager::instance()->GetAce_Proactor(REACTOR_POSTDEFINE));

    //启动中间服务器链接管理器定时器
    App_ClientProConnectManager::instance()->StartConnectTask(App_MainConfig::instance()->GetConnectServerCheck());

    //加载所有的插件初始化动作
    if (false == App_ModuleLoader::instance()->InitModule())
    {
        OUR_DEBUG((LM_INFO, "[CServerManager::Run]App_ModuleLoader::instance()->InitModule() is error.\n"));
        return false;
    }

    //开始消息处理线程
    App_MessageServiceGroup::instance()->Start();

    if(App_MainConfig::instance()->GetConnectServerRunType() == 1)
    {
        //启动异步处理服务器间消息包的过程
        App_ServerMessageTask::instance()->Start();
    }

    //开始启动链接发送定时器
    App_ProConnectManager::instance()->StartTimer();


    //开闸，让客户端数据进来
    if (!App_ProactorManager::instance()->StartClientProactor())
    {
        OUR_DEBUG((LM_INFO, "[CProServerManager::Start]App_ProactorManager::instance()->StartClientProactor is error.\n"));
        return false;
    }

    return true;
}

bool CProServerManager::Close()
{
    OUR_DEBUG((LM_INFO, "[CProServerManager::Close]Close begin....\n"));
    App_ProConnectAcceptManager::instance()->Close();
    OUR_DEBUG((LM_INFO, "[CProServerManager::Close]Close App_ProConnectAcceptManager OK.\n"));

    m_ProConsoleConnectAcceptor.cancel();
    App_TimerManager::instance()->deactivate();
    OUR_DEBUG((LM_INFO, "[CProServerManager::Close]Close App_TimerManager OK.\n"));

    App_ProUDPManager::instance()->Close();
    OUR_DEBUG((LM_INFO, "[CProServerManager::Close]Close App_ProUDPManager OK.\n"));

    App_ClientProConnectManager::instance()->Close();
    OUR_DEBUG((LM_INFO, "[CProServerManager::Close]Close App_ClientProConnectManager OK.\n"));

    App_ModuleLoader::instance()->Close();
    OUR_DEBUG((LM_INFO, "[CProServerManager::Close]Close App_ModuleLoader OK.\n"));

    App_ServerMessageTask::instance()->Close();
    OUR_DEBUG((LM_INFO, "[CProServerManager::Close]Close App_ServerMessageTask OK.\n"));

    App_MessageServiceGroup::instance()->Close();
    OUR_DEBUG((LM_INFO, "[CProServerManager::Close]Close App_MessageServiceGroup OK.\n"));

    App_ProConnectManager::instance()->CloseAll();
    OUR_DEBUG((LM_INFO, "[CProServerManager::Close]Close App_ProConnectManager OK.\n"));

    AppLogManager::instance()->Close();
    OUR_DEBUG((LM_INFO, "[CProServerManager::Close]Close AppLogManager OK\n"));

    App_MessageManager::instance()->Close();
    OUR_DEBUG((LM_INFO, "[CProServerManager::Close]Close App_MessageManager OK.\n"));

    App_BuffPacketManager::instance()->Close();
    OUR_DEBUG((LM_INFO, "[CProServerManager::Close]Close App_BuffPacketManager OK\n"));

    App_ProactorManager::instance()->StopProactor();
    OUR_DEBUG((LM_INFO, "[CProServerManager::Close]Close App_ReactorManager OK.\n"));

    OUR_DEBUG((LM_INFO, "[CProServerManager::Close]Close end....\n"));

    if(NULL != m_pFrameLoggingStrategy)
    {
        m_pFrameLoggingStrategy->EndLogStrategy();
        SAFE_DELETE(m_pFrameLoggingStrategy);
    }

    OUR_DEBUG((LM_INFO, "[CProServerManager::Close]EndLogStrategy end....\n"));

    return true;
}
