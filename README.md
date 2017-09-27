# kill_idle_in_transaction: A custom bgworker for PostgreSQL 9.4 & 9.5

Background worker able to kill connections that are idle in transaction state for a certain amount of time.

This is a modified version of Michael Paquier kill_idle background module.

This worker can use the following parameter to decide the interval of time used to scan and kill idle connections.

1.  *kill_idle_in_transaction.max_idle_time* :  Maximum time allowed for backends to be idle in seconds. Default set at 5s, maximum value is 3600s.


Idle in transaction backend scan is done with a loop operation on pg_stat_activity, running at the same interval of time as the above parameter.
This worker is compatible with PostgreSQL 9.4 and 9.5.


## How to install

1. Clone the repository using following command:
```cmd
git clone https://github.com/vibhorkum/kill_idle_in_transaction
```
2. Set the PATH variable to include location of *pg_config* as given below:
```cmd
export PATH=/usr/pgsql-9.4/bin:$PATH
```
3. Use following command to install the module
```cmd
make
make install
```

### Example is given below:
```cmd
[root@epas96 kill_idle_in_transaction]# make 
gcc -Wall -Wmissing-prototypes -Wpointer-arith -Wdeclaration-after-statement -Wendif-labels -Wmissing-format-attribute -Wformat-security -fno-strict-aliasing -fwrapv -fexcess-precision=standard -I/usr/libexec/icu-ppas-53.1/include -O2 -g -pipe -Wall -Wp,-D_FORTIFY_SOURCE=2 -fexceptions -fstack-protector-strong --param=ssp-buffer-size=4 -grecord-gcc-switches -m64 -mtune=generic -I/usr/include/et -fPIC -I. -I./ -I/usr/ppas-9.4/include/server -I/usr/ppas-9.4/include/internal -I/usr/include/et -D_GNU_SOURCE -I/usr/include/libxml2 -I/usr/libexec/icu-ppas-53.1/include   -I/usr/include  -c -o kill_idle_in_transaction.o kill_idle_in_transaction.c
gcc -Wall -Wmissing-prototypes -Wpointer-arith -Wdeclaration-after-statement -Wendif-labels -Wmissing-format-attribute -Wformat-security -fno-strict-aliasing -fwrapv -fexcess-precision=standard -I/usr/libexec/icu-ppas-53.1/include -O2 -g -pipe -Wall -Wp,-D_FORTIFY_SOURCE=2 -fexceptions -fstack-protector-strong --param=ssp-buffer-size=4 -grecord-gcc-switches -m64 -mtune=generic -I/usr/include/et -fPIC -L/usr/ppas-9.4/lib -Wl,-rpath,/usr/libexec/icu-ppas-53.1/lib -L/usr/libexec/icu-ppas-53.1/lib   -L/usr/lib64 -L/usr/libexec/icu-ppas-53.1/lib  -Wl,--as-needed -Wl,-rpath,'/usr/ppas-9.4/lib',--enable-new-dtags  -shared -o kill_idle_in_transaction.so kill_idle_in_transaction.o
[root@epas96 kill_idle_in_transaction]# make install
/bin/mkdir -p '/usr/ppas-9.4/lib'
/bin/install -c -m 755  kill_idle_in_transaction.so '/usr/ppas-9.4/lib/'
```

After installing the module, please update the following postgresql.conf parameter:
```cmd
shared_preload_libraries = '$libdir/kill_idle_in_transaction'
```
and restart the PostgreSQL service.

