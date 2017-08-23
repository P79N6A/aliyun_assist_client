#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <unistd.h>

#include "jsoncpp/json.h"
#include "utils/CheckNet.h"
#include "utils/FileVersion.h"
#include "utils/http_request.h"
#include "utils/OsVersion.h"
#include "utils/ProcessUtil.h"#include "json11/json11.h"
#include "./gshell.h"
#include "utils/Log.h"
#include "utils/AssistPath.h"
#include "utils/FileUtil.h"
#include "utils/singleton.h"
#include "./schedule_task.h"
#include "optparse/OptionParser.h"
#include "curl/curl.h"
#include "plugin/timer_manager.h"
#include "utils/dump.h"
#include "utils/Encode.h"
#include "../VersionInfo.h"
#include "./xs_shell.h"

#define THREAD_SLEEP_TIME 5
#define PROCESS_MAX_DURATION 60 * 60 * 1000
#define UPDATER_TIMER_DURATION 3600
#define UPDATER_TIMER_DUETIME 30
#define LOGFILE "AliyunAssistDebug.txt"
#define UPDATERFILE "aliyun_assist_update.exe"
#define UPDATERCMD "--check_update"


#define CLOCKID CLOCK_REALTIME 
#define LOCKFILE "/var/run/AliYunAssistService.pid"
#define LOCKMODE (S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH)


static pthread_mutex_t signalQueueMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t terminatedCond = PTHREAD_COND_INITIALIZER;
volatile long gMessageCount = 0;
sigset_t sigMask;
bool gTerminated = false;
th_param param;

bool LaunchProcessAndWaitForExit(char* path,char* commandLines, bool wait) {
  pid_t pid;

  pid = fork();
	if (pid < 0) {
		Log::Error("Failed to fork AliYunAssistService task process: %s",strerror(errno));
		return false;
	} else if (pid == 0) {
		if(execl(path, commandLines,(char * )0) == -1) {
				Log::Error("Failed to launch AliYunAssistService task process: %s",strerror(errno));
				return false;	
			}
	} else if (wait){
		int stat;
		pid_t newPID;
		newPID = waitpid(pid, &stat, 0);
		if (newPID != pid) {
			return false;
		}
	}
	
	return true;
}

void*  ProducerThreadFunc(void*)
{
	int fd;
	size_t sReadBytes;
	char  cReadBuffer[0x1000];

	fd = open("/dev/virtio-ports/org.qemu.guest_agent.0", O_RDONLY, S_IRUSR);

	if (fd != -1) {
		while (true)
		{
			sReadBytes = read(fd, cReadBuffer, sizeof(cReadBuffer));
			if (sReadBytes > 0) {
				cReadBuffer[sReadBytes] = 0;
				//DoParse(cReadBuffer, sReadBytes);
				pthread_mutex_lock(&signalQueueMutex);
				gMessageCount++;
				pthread_mutex_unlock(&signalQueueMutex);
			}

			if (gTerminated)
			{
				break;
			}
			sleep(THREAD_SLEEP_TIME);
		}
	} else {
		printf("device open failure/n");
	}

	close(fd);
	return 0;
}

void* ConsumerThreadFunc(void*) {
  while (true) {
    //When GShell messageg arrives, we launch the executor to process the message.
    if (gMessageCount > 0) {
        Singleton<task_engine::TaskSchedule>::I().Fetch();
        pthread_mutex_lock(&signalQueueMutex);
        gMessageCount--;
        pthread_mutex_unlock(&signalQueueMutex);
    }

    if (gTerminated) {
      break;
    }

    sleep(THREAD_SLEEP_TIME);
  }

  return 0;	
}

void*  SignalProcessingThreadFunc(void* arg)
{
	int errCode, sigNo;

	for (;;) {
		errCode = sigwait(&sigMask, &sigNo);
		if (errCode != 0) {
			Log::Error("Failed to set updater timer interval: %s", strerror(errCode));
		}
		switch (sigNo) {
		case SIGTERM:
			pthread_mutex_lock(&signalQueueMutex);
			gTerminated = true;
			pthread_mutex_unlock(&signalQueueMutex);
			pthread_cond_signal(&terminatedCond);
			pthread_exit(NULL);
			break;
		case SIGUSR1:
      Singleton<task_engine::TaskSchedule>::I().Fetch();
			LaunchProcessAndWaitForExit(UPDATERFILE, UPDATERCMD, false);
			break;
		default:
			//exit(EXIT_FAILURE);
			break;
		}	
	}
}

void* UpdaterThreadFunc(void *arg) {
	timer_t timerID;  
	struct sigevent sEvent; 
	memset(&sEvent, 0, sizeof(struct sigevent));  
	sEvent.sigev_signo = SIGUSR1;  
	sEvent.sigev_notify = SIGEV_SIGNAL;  
	if (timer_create(CLOCKID, &sEvent, &timerID) == -1) {  
		Log::Error("Failed to set updater timer: %s",strerror(errno));
		return (void*)-1;
	}  

	struct itimerspec timerSpec;  
	timerSpec.it_interval.tv_sec = UPDATER_TIMER_DURATION;  
	timerSpec.it_interval.tv_nsec = 0;  
	timerSpec.it_value.tv_sec = UPDATER_TIMER_DUETIME;  
	timerSpec.it_value.tv_nsec = 0;  
	if (timer_settime(timerID, 0, &timerSpec, 0) == -1) {  
		Log::Error("Failed to set updater timer interval: %s",strerror(errno));
		return (void*)-1;
	} 

	pthread_cond_wait(&terminatedCond, &signalQueueMutex);
   
	//clean up codes here    
	if(timer_delete(timerID)== -1) {
		syslog(LOG_ERR,"Failed to delete updater timer: %s",strerror(errno));  	
	}
} 

int IsServiceRunning(void)
{
	int fd;
	char pIDStr[16]	;
	
	fd = open(LOCKFILE, O_RDWR|O_CREAT, LOCKMODE);
	if(fd < 0){
			Log::Error("Failed to open %s: %s",LOCKFILE, strerror(errno));
			return -1;
	}
	
	struct flock fl;
	fl.l_type = F_WRLCK; // д�ļ�����
	fl.l_start = 0;
	fl.l_whence = SEEK_SET;
	fl.l_len = 0;
	int ret = fcntl(fd, F_SETLK, &fl);
	if (ret < 0)
	{
		if (errno == EACCES || errno == EAGAIN)
		{
			Log::Error("Failed to lock %s: %s", LOCKFILE, strerror(errno));
			close(fd);
			return -1;
		}
	}
	
	ftruncate(fd,0);
	sprintf(pIDStr,"%ld",(long)getpid());
	write(fd,pIDStr,strlen(pIDStr) + 1);
	return 0;
}


/*Create the Deamon Service*/
int BecomeDeamon()
{
	pid_t pid, sid;
	int i = 0;
	struct rlimit	fileDescriptorLimit;
	struct sigaction sigActionMask;

	/*Get the maximum limit of the file descriptors*/
	if (getrlimit(RLIMIT_NOFILE, &fileDescriptorLimit) < 0) {
		Log::Error("Failed to get file descriptor maximum limit: %s", strerror(errno));
	}

	/* Fork off the parent process and exit the parent process*/
	pid = fork();
	if (pid < 0) {
    Log::Info("pid < 0 quit");
		exit(EXIT_FAILURE);
	}
	else if (pid > 0) {
    Log::Info("pid > 0 quit");
		exit(EXIT_SUCCESS);
	}

  Log::Info("deamon running");
	/*Set the default file access mask*/
	umask(0);

	/* Create a new SID for the child process */
	sid = setsid();
	if (sid < 0) {
		Log::Error("Failed to create Session for AliYunAssistService child process: %s", strerror(errno));
		exit(EXIT_FAILURE);
	}

	/* Change the current working directory */
	if ((chdir("/")) < 0) {
		Log::Error("Failed to change working directory for AliYunAssistService: %s", strerror(errno));
		exit(EXIT_FAILURE);
	}

	/*close the file descriptors inheritting from parent process*/
	if (fileDescriptorLimit.rlim_max == RLIM_INFINITY) {
		fileDescriptorLimit.rlim_max = 1024;
	}
	for (i = 0; i < fileDescriptorLimit.rlim_max; i++) {
		close(i);
	}

	/* Close out the standard file descriptors */
	close(STDIN_FILENO);
	close(STDOUT_FILENO);
	close(STDERR_FILENO);

	/*Cut off the connection with tty*/
	sigActionMask.sa_handler = SIG_IGN;
	sigemptyset(&sigActionMask.sa_mask);
	sigActionMask.sa_flags = 0;
	if (sigaction(SIGHUP, &sigActionMask, NULL) < 0) {
		Log::Error("Failed to mask tty controls for AliYunAssistService: %s", strerror(errno));
	}

	return 0;
}

int InitService()
{
    Log::Info("InitService");
  Singleton<task_engine::TimerManager>::I().Start();
  Singleton<task_engine::TaskSchedule>::I().Fetch();
  Singleton<task_engine::TaskSchedule>::I().FetchPeriodTask();

	int ret = 0;
	pthread_t pUpdaterThread, pConsumerThread, pProducerThread, pSignalProcessingThread, pXenCmdExecThread, pXenCmdReadThread;

	ret = pthread_create(&pUpdaterThread, NULL, UpdaterThreadFunc, NULL);
	if (ret != 0) {
		Log::Error("Failed to create AliYunAssistService updater thread: %s", strerror(errno));
		return -1;
	}

	ret = pthread_create(&pConsumerThread, NULL, ConsumerThreadFunc, NULL);
	if (ret != 0) {
		Log::Error("Failed to create AliYunAssistService consumer thread: %s", strerror(errno));
		return -1;
	}

	ret = pthread_create(&pProducerThread, NULL, ProducerThreadFunc, NULL);
	if (ret != 0) {
		Log::Error("Failed to create AliYunAssistService producer thread: %s", strerror(errno));
		return -1;
	}

	ret = pthread_create(&pSignalProcessingThread, NULL, SignalProcessingThreadFunc, NULL);
	if (ret != 0) {
		Log::Error("Failed to create AliYunAssistService signal processing thread: %s", strerror(errno));
		return -1;
	}

    param.bTerminated = &gTerminated;
    param.kicker = []() {
    pthread_mutex_lock(&signalQueueMutex);
    gMessageCount++;
    pthread_mutex_unlock(&signalQueueMutex);
    };

    Log::Info("Call XSShellStart");
    ret = XSShellStart(&param, &pXenCmdExecThread, &pXenCmdReadThread);
    if (ret != 1) {
		Log::Error("XSShellStart Failed: %d", ret);
		return -1;
	}

	ret = pthread_join(pUpdaterThread, NULL);
	if (ret != 0) {
		Log::Error("Failed to join the AliYunAssistService updater thread: %s", strerror(errno));
		return -1;
	}

	ret = pthread_join(pConsumerThread, NULL);
	if (ret != 0) {
		Log::Error("Failed to join the AliYunAssistService comsumer thread: %s", strerror(errno));
		return -1;
	}

	ret = pthread_join(pProducerThread, NULL);
	if (ret != 0) {
		Log::Error("Failed to join the AliYunAssistService producer thread: %s", strerror(errno));
		return -1;
	}

	ret = pthread_join(pSignalProcessingThread, NULL);
	if (ret != 0) {
		Log::Error("Failed to join the AliYunAssistService signal processing thread: %s", strerror(errno));
		return -1;
	}

  ret = pthread_join(pXenCmdExecThread, NULL);
  if (ret != 0) {
    Log::Error("Failed to join the AliYunAssistService xen cmd exec thread: %s", strerror(errno));
    return -1;
  }

  ret = pthread_join(pXenCmdReadThread, NULL);
  if (ret != 0) {
    Log::Error("Failed to join the AliYunAssistService xen cmd read thread: %s", strerror(errno));
    return -1;
  }
}

using optparse::OptionParser;

OptionParser& initParser() {
  static OptionParser parser = OptionParser().description("");

  parser.add_option("-v", "--version")
    .dest("version")
    .action("store_true")
    .help("show version and exit");

  parser.add_option("-fetch_task", "--fetch_task")
    .action("store_true")
    .dest("fetch_task")
    .help("fetch tasks from server and run tasks");

  parser.add_option("-d", "--deamon")
    .action("store_true")
    .dest("deamon")
    .help("start as deamon");

  return parser;
}


int main(int argc, char *argv[]) {
  AssistPath path_service("");
  std::string log_path = path_service.GetLogPath();
  log_path += FileUtils::separator();
  log_path += "aliyun_assist_main.log";
  Log::Initialise(log_path);
  Log::Info("main begin...");

  OptionParser& parser = initParser();
  optparse::Values options = parser.parse_args(argc, argv);

  if (options.is_set("version")) {
    printf("%s", FILE_VERSION_RESOURCE_STR);
    return 0;
  }
  curl_global_init(CURL_GLOBAL_ALL);
  HostChooser  host_choose;
  bool found = host_choose.Init(path_service.GetConfigPath());
  if (!found) {
    Log::Error("could not find a match region host");
  }
  if (options.is_set("deamon")) {
    Log::Info("in deamon mode");
    struct sigaction sigActionUpdate;
    sigset_t sigOldMask;

    /*Ensure AliYunAssistService run in Singleton mode*/	
    if(ProcessUtils::is_single_proc_inst_running("aliyun-service") == false) {
      Log::Error("Failed to launch AliYunAssistService more than one instance");
      exit(EXIT_SUCCESS);
    }

    /*Create the daemon service*/
    BecomeDeamon();

    /*Process SIGTERM and SIGUSR1 signals in a seperate signal processing thread and block them in all other threads */
    sigemptyset(&sigMask);
    sigaddset(&sigMask, SIGTERM);
    sigaddset(&sigMask, SIGUSR1);
    if (pthread_sigmask(SIG_BLOCK, &sigMask, &sigOldMask) != 0) {
      Log::Error("Failed to set signal mask for AliYunAssistService: %s", strerror(errno));
      exit(EXIT_FAILURE);
   }

    if (pthread_sigmask(SIG_SETMASK, &sigOldMask, NULL) != 0) {
      Log::Error("Failed to reset signal mask: %s", strerror(errno));
    }
    /*Initialize the service*/
    InitService();

    Log::Info("exit deamon");
    exit(EXIT_SUCCESS);
    return 0;
  } else if (options.is_set("fetch_task")) {
    Singleton<task_engine::TaskSchedule>::I().Fetch();
    Singleton<task_engine::TaskSchedule>::I().FetchPeriodTask();
    sleep(3600);
    return 0;
  }

  curl_global_cleanup();
  parser.print_help();
  return 0;
}