#!/usr/bin/expect
set timeout -1  
set branch    [lindex $argv 0]
set commit   [lindex $argv 1]
set version  [lindex $argv 2]
spawn ssh Administrator@100.69.210.206
expect {
"yes/no" {send "yes\r";}
"*password:" {send "tC123456\r";}
}
expect "*>"
send "cd C:\\assist_build \r"
expect "*>"
send "python build.py $branch $commit $version \r"
#send "where git \r"
expect "*>"
send "exit \r"
expect eof
