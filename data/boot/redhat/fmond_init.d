#!/bin/bash
#
# fmond		Start up the file monitoring daemon
#
# chkconfig: 2345 55 25
# description: This service starts up the file monitoring daemon.
### BEGIN INIT INFO
# Provides: fmond
# Required-Start: $local_fs $network $syslog
# Required-Stop: $local_fs $syslog
# Should-Start: $syslog
# Should-Stop: $network $syslog
# Default-Start: 2 3 4 5
# Default-Stop: 0 1 6
# Short-Description: Start up the file monitoring daemon
# Description:       This service starts up the file monitoring daemon.
### END INIT INFO

# Source function library.
. /etc/init.d/functions

RETVAL=0
PIDFILE=/var/run/fmond/fmond.pid

prog=fmond
exec=/usr/sbin/fmond
lockfile=/var/lock/subsys/$prog

# Source config
if [ -f /etc/sysconfig/$prog ] ; then
    . /etc/sysconfig/$prog
fi

start()
{
	[ -x $exec ] || exit 5
	[ -f /etc/fmond.conf ] || exit 6
	if [ "$STARTUP" = "1" ]; then
		echo -n $"Starting file monitoring daemon: "
		daemon --pidfile="$PIDFILE" $exec -i "$PIDFILE"
		RETVAL=$?
		echo
		[ $RETVAL -eq 0 ] && touch $lockfile
	fi
	return $RETVAL
}

stop()
{
	echo -n $"Stopping file monitoring daemon: "
	killproc -p "$PIDFILE" $exec
	RETVAL=$?
	echo
	[ $RETVAL -eq 0 ] && rm -f $lockfile
	return $RETVAL
}

reload()
{
    RETVAL=1
    fmond=$(cat "${PIDFILE}" 2>/dev/null)
    echo -n "Reloading file monitoring daemon..."
    if [ -n "${fmond}" ] && [ -e /proc/"${fmond}" ]; then
	kill -HUP "$fmond";
	RETVAL=$?
    fi
    if [ $RETVAL -ne 0 ]; then
	failure
    else
	success
    fi
    echo
    return $RETVAL
}

rhstatus() {
        status -p "$PIDFILE" -l $prog $exec
}

restart() {
        stop
        start
}

case "$1" in
  start)
        start
        ;;
  stop)
        stop
        ;;
  restart)
        restart
        ;;
  reload|force-reload)
	reload
	;;
  status)
        rhstatus
        ;;
  condrestart|try-restart)
        rhstatus >/dev/null 2>&1 || exit 0
        restart
        ;;
  *)
        echo $"Usage: $0 {start|stop|restart|condrestart|try-restart|reload|force-reload|status}"
        exit 2
esac

exit $?
