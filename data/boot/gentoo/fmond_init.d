#!/sbin/runscript

extra_started_commands="reload"

depend() {
    need localmount
    after bootmisc
}

start() {
		if [ "${STARTUP}" = "1" ]; then
			ebegin "Starting fmond"
			start-stop-daemon --start --pidfile /var/run/fmond/fmond.pid \
				--exec /usr/sbin/fmond -- -c /etc/fmond.conf
			eend $? "Failed to start fmond"
		fi
}

stop() {
        ebegin "Stopping fmond"
        start-stop-daemon --stop --pidfile /var/run/fmond/fmond.pid
        eend $? "Failed to stop fmond"
}

reload() {
        ebegin "Reloading fmond"
        kill -HUP `cat /var/run/fmond/fmond.pid` &>/dev/null
        eend $? "Failed to reload fmond"
}
