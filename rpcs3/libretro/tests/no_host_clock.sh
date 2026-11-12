#!/bin/sh
# The core's own code paces nothing by the host: no clock read and no sleep.
# The frontend's retro_run is the only clock, and a wait waits on an event,
# untimed. Comments are not searched. A line that reads the date for what
# it is, not to time anything (a photo named by the date), says so in a
# comment with "not pacing:".
#
# no_host_clock.sh FILE...
pattern='sleep_for|sleep_until|usleep[ ]*\(|nanosleep|[^A-Za-z_]Sleep[ ]*\(|SleepEx|steady_clock|system_clock|high_resolution_clock|get_system_time[ ]*\(|get_guest_system_time|clock_gettime|gettimeofday|QueryPerformanceCounter|GetTickCount|timeGetTime|cpu_features_get_time_usec|thread_ctrl::wait_for[ ]*\(|time[ ]*\([ ]*(NULL|nullptr|0)[ ]*\)'
status=0
for f in "$@"; do
   hits=$(awk -v pat="$pattern" -v file="$f" '
   {
      raw = $0; code = ""; line = $0
      while (length(line))
      {
         if (incomment)
         {
            e = index(line, "*/")
            if (!e) { line = ""; break }
            line = substr(line, e + 2); incomment = 0
            continue
         }
         b = index(line, "/*"); s = index(line, "//")
         if (s && (!b || s < b)) { code = code substr(line, 1, s - 1); break }
         if (b) { code = code substr(line, 1, b - 1); line = substr(line, b + 2); incomment = 1; continue }
         code = code line; break
      }
      if (code ~ pat && raw !~ /not pacing:/)
         printf "%s:%d: %s\n", file, NR, raw
   }' "$f")
   if [ -n "$hits" ]; then
      echo "$hits"
      status=1
   fi
done
[ $status -eq 0 ] && echo "no host clock: $# files, none reads the host's clock or sleeps"
exit $status
