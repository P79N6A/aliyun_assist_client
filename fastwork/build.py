import os
import shutil
import time
import stat
import sys
import ConfigParser

major_version = "0";
minor_version = "0"
patch_version = "0"
build_number  = "0"
version   = "0.0.0.0"

g_fastwork = os.path.split(os.path.realpath(__file__))[0];

def exec_cmd(cmd): 
    print cmd;
    r = os.popen(cmd)  
    output = r.read()  
    r.close()  
    print output

def getBuildVersion():
    global major_version,minor_version,patch_version,build_number,version
    cf = ConfigParser.ConfigParser()
    cf.read("version.ini")
    major_version = cf.get("assist_version", "major_version_number")
    minor_version = cf.get("assist_version", "minor_version_number")
    patch_version = cf.get("assist_version", "patch_number")

    f = open("aone_version")
    build_number = f.read().strip('\n');
    version = major_version + "." + minor_version + "." + \
        patch_version + "." + build_number;

    return


def linuxBuild():
    cmd = "ASSIST_VERSION_MAJOR=%s \
        USE_STATIC_LINK=1 \
        ASSIST_VERSION_MINOR=%s \
        ASSIST_VERSION_PATCH=%s \
        ASSIST_VERSION_REVISION=%s \
        cmake ." \
        % (major_version, minor_version, patch_version, build_number);

    os.system(cmd)
    os.system("cmake ../")
    os.system("make");
    os.system("echo '%s' > ../output/init/version" %version )
    
    #make zip
    os.system("mkdir zip");
    os.chdir("zip");
    os.system("echo '%s' > version" % version)
    shutil.copytree("../../output", version)
    cmd = "zip -r linux_update_%s.zip * " % version;
    exec_cmd(cmd)
    os.chdir(g_fastwork);
    
    #make deb
    os.mkdir("deb")
    os.chdir("deb");
    shutil.copytree("../../output", "usr/local/share/aliyun-assist/%s" % version)
    os.system('echo %s > "./usr/local/share/aliyun-assist/version"' % version)
    cmd = " fpm -s dir -t deb -n aliyun_assist -v %s -a all --vendor 'Aliyun Software Foundation' \
            --description 'aliyun assist client' -p aliyun_assist_%s.deb --license 'GPL' -C ./ \
            --after-install usr/local/share/aliyun-assist/%s/init/install \
            --after-remove usr/local/share/aliyun-assist/%s/init/uninstall " % (version, version, version, version)
    exec_cmd(cmd)
    os.chdir(g_fastwork);
    
    #make rpm
    os.mkdir("rpm")
    os.chdir("rpm");
    shutil.copytree("../../output", "usr/local/share/aliyun-assist/%s" % version)
    os.system('echo %s > "./usr/local/share/aliyun-assist/version"' % version)
    cmd = " fpm -s dir -t rpm -n aliyun_assist -v %s -a noarch --vendor 'Aliyun Software Foundation' \
            --description 'aliyun assist client' -p aliyun_assist_%s.rpm --license 'GPL' -C ./ \
            --after-install usr/local/share/aliyun-assist/%s/init/install \
            --after-remove usr/local/share/aliyun-assist/%s/init/uninstall" % (version, version, version, version)
    exec_cmd(cmd)

    return
    


if __name__ == '__main__':
    os.chdir(g_fastwork);
    getBuildVersion();
    linuxBuild();


