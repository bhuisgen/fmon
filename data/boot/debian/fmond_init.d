#!/bin/sh

### BEGIN INIT INFO
# Provides:          fmond
# Required-Start:    $local_fs $remote_fs $network $syslog
# Required-Stop:     $local_fs $remote_fs $network $syslog
# Default-Start:     2 3 4 5
# Default-Stop:      0 1 6
# Short-Description: starts the file monitoring daemon
# Description:       starts fmond using start-stop-daemon
### END INIT INFO

PATH=/usr/local/sbin:/usr/local/bin:/sbin:/bin:/usr/sbin:/usr/bin
DAEMON=/usr/sbin/fmond
DAEMON_OPTS=
PIDFILE=/var/run/fmond/fmond.pid
NAME=fmond
DESC="file monitoring daemon"

set -e

. /lib/lsb/init-functions

[ -x $DAEMON ] || exit 0
[ -r /etc/default/fmond ] && . /etc/default/fmond

case "$1" in
	start)
		if [ "$STARTUP" = "1" ]; then
			echo -n "Starting $DESC: "

			start-stop-daemon --start --quiet --pidfile $PIDFILE \
		    	--exec $DAEMON -- $DAEMON_OPTS || true
			echo "$NAME."
		fi
		;;
		
	stop)
		echo -n "Stopping $DESC: "
		start-stop-daemon --stop --quiet --pidfile $PIDFILE \
	    	--exec $DAEMON || true
		echo "$NAME."
		;;

	restart|force-reload)
		echo -n "Restarting $DESC: "
		start-stop-daemon --stop --quiet --pidfile $PIDFILE \
			--exec $DAEMON || true
		sleep 1

		start-stop-daemon --start --quiet --pidfile $PIDFILE \
			--exec $DAEMON -- $DAEMON_OPTS || true
		echo "$NAME."
		;;

	reload)
		echo -n "Reloading $DESC configuration: "
		start-stop-daemon --stop --signal HUP --quiet --pidfile $PIDFILE \
		    --exec $DAEMON || true
		echo "$NAME."
		;;

	status)
		status_of_proc -p $PIDFILE "$DAEMON" fmond && exit 0 || exit $?
		;;
	*)
		echo "Usage: $NAME {start|stop|restart|reload|force-reload|status}" >&2
		exit 1
		;;
esac

exit 0
