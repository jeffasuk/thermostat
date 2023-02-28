#!/usr/bin/bash
# Licensed under GNU General Public License v3.0
# See https://github.com/jeffasuk/thermostat
# jeff at jamcupboard.co.uk
#
# For development use; not part of production code.
#
# Test CGI script for receiving settings submission from the form on the thermostat's home page.
# This allows testing of the home.html page in a desktop environment.
# Install this as a CGI on http://<your-host>/settings
# If your web server insists on a .cgi extension, use URL rewriting to map the above
# URL to the CGI script (at least that works in Apache2). Or change the submit URL temporarily in home.html.
# Install the file home.html in the same directory on your web server.
#
# After installation, visit the home page. You can then submit the form and see the result in
# the "status" file in the same directory. You can change what's on the display by editing the
# status file, to simulate state changes in the thermostat.
# Here's a way to make continual changes with a reasonable waveform:
# while sleep 4
# do val=$(echo 'scale=4; sec='$(date +%S)' - 30; mods=sqrt(sec*sec); 10 + sqrt(mods) / 3'| bc)
#    sed -i "/<tmp/ s/>[0-9.]*</>$val</" /var/www/html/status
# done
#
# (The above description works in Apache2. A different approach may be needed on other web servers.)

# example QUERY_STRING: des_temp=11&mode=0&precision=0.20&maxreporttime=12
#   des_temp=11
#   mode=0
#   precision=0.20
#   maxreporttime=12

if test ! -f status
then
    # create initial dummy status file
    cat <<EnD >status
<status>
 <tmp id="28FF6133811605E4">12.00</tmp>
 <state>1</state>
 <des>11.00</des>
 <prec>0.20</prec>
 <mode>heating</mode>
 <maxrep>120</maxrep>
</status>
EnD
fi

awk -v $(echo $QUERY_STRING | sed 's/&/ -v /g') '
    function rep(tag, val)
    {
        if (val != "")
        {
            print "<" tag ">" val "</" tag ">";
            next;
        }
    }
    /<des>/ {rep("des", des_temp)}
    /<mode>/ {rep("mode", mode == "0" ? "heating" : "cooling" )}
    /<prec>/ {rep("prec", precision)}
    /<maxrep>/ {rep("maxrep", maxreporttime)}
    {print}
' status >status.new && mv status.new status

# Tell browser to reload previous page
echo Status:302
echo Location:$HTTP_REFERER
echo
