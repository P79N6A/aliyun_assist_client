import os
import shutil
import time
import sys

g_rootdir  = os.path.split(os.path.realpath(__file__))[0]
g_devcom   = '"C:\\Program Files (x86)\\Microsoft Visual Studio 14.0\\Common7\\IDE\\devenv.com"'
g_signcmd  = "client.exe -s 10.137.59.13:8888 -u feng.changf1@alibaba-inc.com -k 9cc447367b85bc927a03d5da31b35c62 "
g_nsis     = '"C:\Program Files (x86)\NSIS\Unicode\makensis.exe"'

def modify_nsi(version):
    patten = "!define PRODUCT_VERSION"
    lines=open("install.nsi",'r').readlines()
    nlen=len(lines)-1
    for i in range(nlen):
        if patten in lines[i]:
            lines[i]=patten +' "%s" \n' % version
    open("install.nsi",'w').writelines(lines)


def win_build(branch,commitid,version):
    vers = version.split(".")
    major_version   = vers[0]
    minor_version   = vers[1]
    patch_version   = vers[2]
    revison_version = vers[3]

    #checkout code
    os.system( "git clone -b %s git@gitlab.alibaba-inc.com:ecs-image/aliyun-assist-client-opensource.git --single-branch %s " % (branch,version) )
    os.chdir( version ) 
    os.system( "git checkout %s" %  commitid) 
    os.system("cmake ./")
    cmd = " set ASSIST_VERSION_MAJOR=%s & set ASSIST_VERSION_MINOR=%s & \
            set ASSIST_VERSION_PATCH=%s & set ASSIST_VERSION_REVISION=%s & \
            cmake ."  % (major_version, minor_version, patch_version, revison_version);
    os.system(cmd)
    
    #build
    cmd = g_devcom +" assist.sln /Rebuild Release ";
    os.system(cmd)
    
    #sign output
    cmd = g_signcmd + "-d output  -n win_backup_%s.zip" % version;
    os.system(cmd);
    cmd = "7z.exe x win_backup_%s.zip  -y" % version;
    os.system(cmd);
    
    #make update zip
    os.chdir( "output" ) 
    os.system("del *.txt *.pdb /f /s /q")
    update_zip_name =  "win_update_%s.zip" % version;
    cmd = "7z.exe a %s *.*  config" % update_zip_name
    os.system(cmd)
    os.system("mv %s ../" % update_zip_name)
    os.chdir( "../" ) 

    #make exe
    modify_nsi(version)
    cmd = g_nsis +' install.nsi'
    os.system( cmd );
    os.system( g_signcmd + "-d aliyun_agent.exe  -n aliyun_agent.zip" ) ;
    os.system( "7z.exe x aliyun_agent.zip  -y")
    os.rename("aliyun_agent.exe","win_agent_%s.exe" % version )

    #clean
    os.system("del commit-*.zip aliyun_agent.zip /f /s /q")


if __name__ == '__main__':
    os.chdir(g_rootdir)
    if len(sys.argv) != 4:
        exit(-1);
    branch = sys.argv[1]
    commit = sys.argv[2]
    version = sys.argv[3] 
    #win_build("feature/1.2","f0cc53b01892de4b51bd28eb962897594219b5bd","1.2.3.6")
    win_build(branch,commit,version)

