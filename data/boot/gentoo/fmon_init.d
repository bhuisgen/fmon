#!/sbin/runscript

extra_started_commands="reload"

depend() {
    need localmount
    after bootmisc
}

start() {
		if [ "${STARTUP}" = "1" ]; then
			ebegin "Starting fmon"
			start-stop-daemon --start --pidfile /var/run/fmon/fmon.pid \
				--exec /usr/sbin/fmon -- -c /etc/fmon.conf
			eend $? "Failed to start fmon"
		fi
}

stop() {
        ebegin "Stopping fmon"
        start-stop-daemon --stop --pidfile /var/run/fmon/fmon.pid
        eend $? "Failed to stop fmon"
}

reload() {
        ebegin "Reloading fmon"
        kill -HUP `cat /var/run/fmon/fmon.pid` &>/dev/null
        eend $? "Failed to reload fmon"
}
