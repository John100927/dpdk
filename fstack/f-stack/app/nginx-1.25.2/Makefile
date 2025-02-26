
default:	build

clean:
	rm -rf Makefile objs

.PHONY:	default clean

build:
	$(MAKE) -f objs/Makefile

install:
	$(MAKE) -f objs/Makefile install

modules:
	$(MAKE) -f objs/Makefile modules

upgrade:
	/usr/local/nginx_fstack/sbin/nginx -t

	kill -USR2 `cat /usr/local/nginx_fstack/logs/nginx.pid`
	sleep 1
	test -f /usr/local/nginx_fstack/logs/nginx.pid.oldbin

	kill -QUIT `cat /usr/local/nginx_fstack/logs/nginx.pid.oldbin`

.PHONY:	build install modules upgrade

#CFLAGS += -I/home/oslab/Desktop/john/leveldb2/leveldb/include
