/* One asynchronous route check per gateway, never per point. No DNS lookup:
 * use the actual MQTT socket's peer, source address and routing mark. */
#if defined(__linux__) && !defined(GK_UPLINK_TEST)
#include <sys/socket.h>
#include <sys/wait.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
static pid_t uplink_pid;
static int uplink_fd=-1;
static uint64_t uplink_next, uplink_deadline;
static void uplink_unknown(void)
{
    g_mobile_profile=true;
    safe_copy(g_uplink,sizeof(g_uplink),"unknown");
}
static void uplink_stop(void)
{
    if(uplink_pid>0) { kill(-uplink_pid,SIGKILL); kill(uplink_pid,SIGKILL); }
    if(uplink_fd>=0) { close(uplink_fd); uplink_fd=-1; }
    /* Reap asynchronously in collect; never block BACnet on a helper. */
}
static void uplink_collect(uint64_t now)
{
    if(!uplink_pid)return;
    int status;
    pid_t done=waitpid(uplink_pid,&status,WNOHANG);
    if(done==0) {
        if(now>=uplink_deadline) { uplink_stop();uplink_unknown(); }
        return;
    }
    char result[96]={0}, dev[64]={0};int mobile=-1;
    ssize_t n=uplink_fd>=0?read(uplink_fd,result,sizeof(result)-1):-1;
    if(uplink_fd>=0)close(uplink_fd);
    uplink_fd=-1;uplink_pid=0;
    if(done>0 && WIFEXITED(status) && WEXITSTATUS(status)==0 && n>0 &&
       sscanf(result,"%d %63s",&mobile,dev)==2 && (mobile==0 || mobile==1)) {
        if(g_mobile_profile!=(mobile==1) || strcmp(g_uplink,dev))
            LOG_INFOF("MQTT send profile=%s interface=%s",mobile?"mobile":"normal",dev);
        g_mobile_profile=mobile==1;safe_copy(g_uplink,sizeof(g_uplink),dev);
    } else uplink_unknown();
}
static void uplink_tick(uint64_t now)
{
    if(uplink_pid || now<uplink_next)return;
    uplink_next=now+5000;
    int fd=mosquitto_socket(g_mosq);
    struct sockaddr_storage peer,local;socklen_t size=sizeof(peer);
    char dst[INET6_ADDRSTRLEN],src[INET6_ADDRSTRLEN],mark_text[24];
    if(fd<0 || getpeername(fd,(struct sockaddr*)&peer,&size)) { uplink_unknown();return; }
    size=sizeof(local);
    if(getsockname(fd,(struct sockaddr*)&local,&size)) { uplink_unknown();return; }
    int af=peer.ss_family;
    if(af!=AF_INET && af!=AF_INET6) { uplink_unknown();return; }
    const void *pa=af==AF_INET?(void*)&((struct sockaddr_in*)&peer)->sin_addr:(void*)&((struct sockaddr_in6*)&peer)->sin6_addr;
    const void *la=af==AF_INET?(void*)&((struct sockaddr_in*)&local)->sin_addr:(void*)&((struct sockaddr_in6*)&local)->sin6_addr;
    if(!inet_ntop(af,pa,dst,sizeof(dst)) || !inet_ntop(af,la,src,sizeof(src)))return;
    unsigned mark=0;size=sizeof(mark);
    getsockopt(fd,SOL_SOCKET,SO_MARK,&mark,&size);
    snprintf(mark_text,sizeof(mark_text),"%u",mark);
    int pipes[2];if(pipe(pipes))return;
    pid_t pid=fork();
    if(pid==0) {
        setpgid(0,0);dup2(pipes[1],STDOUT_FILENO);
        close(pipes[0]);close(pipes[1]);
        /* Do not leak MQTT sockets or other daemon descriptors to Lua. */
        long limit=sysconf(_SC_OPEN_MAX);if(limit<0)limit=1024;
        for(int i=3;i<limit;i++)close(i);
        execl("/usr/bin/lua","lua","/usr/sbin/gk-mqtt-uplink.lua",dst,src,mark_text,(char*)NULL);
        _exit(127);
    }
    close(pipes[1]);
    if(pid<0) { close(pipes[0]);return; }
    setpgid(pid,pid);fcntl(pipes[0],F_SETFL,O_NONBLOCK);
    uplink_pid=pid;uplink_fd=pipes[0];uplink_deadline=now+3000;
}
#else
static void uplink_collect(uint64_t now) { (void)now; }
static void uplink_tick(uint64_t now) { (void)now; }
static void uplink_stop(void) {}
#endif
