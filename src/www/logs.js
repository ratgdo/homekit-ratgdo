/***********************************************************************
 * homekit-ratgdo logger web page javascript functions
 *
 * Copyright (c) 2024-25 David Kerr, https://github.com/dkerr64
 *
 */

// Global vars...
var evtSource = undefined;      // for Server Sent Events (SSE)
var msgJson = undefined;        // for status
const clientUUID = uuidv4();    // uniquely identify this session

function findStartTime(text) {
    const regex = /[\s\r\n]/;
    let serverTime = undefined;
    let upTime = undefined;
    let bootTime = undefined;

    function search(string, regexp, from = 0) {
        const index = string.slice(from).search(regexp);
        return index === -1 ? -1 : index + from;
    }

    let i = text.indexOf(':', text.indexOf('Server uptime')) + 2;
    let j = search(text, regex, i);
    upTime = Number(text.substring(i, j)); // Value is in milliseconds

    i = text.indexOf('Server time');
    if (i >= 0) {
        i = text.indexOf(':', i) + 2;
        let j = search(text, regex, i);
        serverTime = Number(text.substring(i, j)) * 1000; // Convert to milliseconds
        bootTime = serverTime - upTime;
    }

    return {
        serverTime: serverTime,
        upTime: upTime,
        bootTime: bootTime
    };
}

function msToTime(duration) {
    let seconds = Math.floor((duration / 1000) % 60),
        minutes = Math.floor((duration / (1000 * 60)) % 60),
        hours = Math.floor((duration / (1000 * 60 * 60)) % 24),
        days = Math.floor((duration / (1000 * 60 * 60 * 24)));

    hours = (hours < 10) ? "0" + hours : hours;
    minutes = (minutes < 10) ? "0" + minutes : minutes;
    seconds = (seconds < 10) ? "0" + seconds : seconds;

    return days + " days " + hours + " hrs " + minutes + " mins " + seconds + " secs";
}

function openTab(evt, tabName) {
    var i, tabcontent, tablinks;
    // Get all elements with class="tabcontent" and hide them
    tabcontent = document.getElementsByClassName("tabcontent");
    for (i = 0; i < tabcontent.length; i++) {
        tabcontent[i].style.display = "none";
    }
    document.getElementById("clearLogBtn").style.display = "none";
    document.getElementById("reloadLogButton").style.display = "none";
    document.getElementById("clearBtn").style.display = "none";
    // Get all elements with class="tablinks" and remove the class "active"
    tablinks = document.getElementsByClassName("tablinks");
    for (i = 0; i < tablinks.length; i++) {
        tablinks[i].className = tablinks[i].className.replace(" active", "");
    }
    // Show the current tab, and add an "active" class to the button that opened the tab
    document.getElementById(tabName).style.display = "block";
    evt.currentTarget.className += " active";
    if (tabName === "logTab") {
        document.getElementById("clearLogBtn").style.display = "inline-block";
        document.getElementById("reloadLogButton").style.display = "inline-block";
    } else if (tabName === "crashTab") {
        if (msgJson?.crashCount != 0) {
            document.getElementById("clearBtn").style.display = "inline-block";
        }
    } else if (tabName === "statusTab") {
        // Refresh status from the server
        loaderElem.style.visibility = "visible";
        document.getElementById("statusjson").innerText = "";
        fetch("status.json")
            .then((response) => {
                if (!response.ok || response.status !== 200) {
                    reject(`Error requsting status.json, RC: ${response.status}`);
                } else {
                    return response.text();
                }
            })
            .then((text) => {
                msgJson = JSON.parse(text);
                document.getElementById("statusjson").innerText = text;
                loaderElem.style.visibility = "hidden";
            })
            .catch(error => console.warn(error));
    }
}

async function loadLogs() {
    // Load all the logs in parallel, showing progress indicator while we do...
    let serverBootTime = undefined;
    loaderElem.style.visibility = "visible";
    console.log("Start loading server logs and status");
    Promise.allSettled([
        fetch("rest/events/subscribe?id=" + clientUUID + "&log=1")
            .then((response) => {
                if (!response.ok || response.status !== 200) {
                    reject(`Error registering for Server Sent Events, RC: ${response.status}`);
                } else {
                    return response.text();
                }
            })
            .then((text) => {
                const evtUrl = text + '?id=' + clientUUID;
                console.log(`Register for server sent events at ${evtUrl}`);
                evtSource = new EventSource(evtUrl);
                evtSource.addEventListener("logger", (event) => {
                    let divElem = document.getElementById("logTab");
                    let scroll = (divElem.scrollHeight - divElem.scrollTop - divElem.clientHeight) < 10;
                    document.getElementById("showlog").insertAdjacentText('beforeend', event.data + "\n");
                    // Only scroll the page if we are already at bottom of the page
                    if (scroll) divElem.scrollTop = divElem.scrollHeight;
                });
                evtSource.addEventListener("error", (event) => {
                    // If an error occurs close the connection.
                    console.log(`SSE error occurred while attempting to connect to ${evtSource.url}`);
                    evtSource.close();
                });
            })
            .catch(error => console.warn(error)),

        fetch("showlog")
            .then((response) => {
                if (!response.ok || response.status !== 200) {
                    reject(`Error requsting logs, RC: ${response.status}`);
                } else {
                    return response.text();
                }
            })
            .then((text) => {
                const elem = document.getElementById("showlog");
                const header = document.getElementById("showLogHeader");
                const { serverTime, upTime, bootTime } = findStartTime(text);
                serverBootTime = bootTime;
                if (bootTime) {
                    let date = new Date();
                    date.setTime(bootTime);
                    header.insertAdjacentHTML('beforeend', `<pre style="margin: 0px">Server started at:   ${date.toUTCString()}</pre>`);
                    date.setTime(serverTime);
                    header.insertAdjacentHTML('beforeend', `<pre style="margin: 0px">Server current time: ${date.toUTCString()}</pre>`);
                }
                if (upTime) {
                    header.insertAdjacentHTML('beforeend', `<pre style="margin: 0px">Server upTime:       ${msToTime(upTime)}</pre>`);
                }
                elem.insertAdjacentText('afterbegin', text);
                let divElem = document.getElementById("logTab");
                // Scroll to the bottom
                divElem.scrollTop = divElem.scrollHeight;
            })
            .catch(error => console.warn(error)),

        fetch("status.json")
            .then((response) => {
                if (!response.ok || response.status !== 200) {
                    reject(`Error requsting status.json, RC: ${response.status}`);
                } else {
                    return response.text();
                }
            })
            .then((text) => {
                msgJson = JSON.parse(text);
                document.getElementById("deviceName").innerHTML = msgJson.deviceName + " Logs";
                document.title = msgJson.deviceName + " Logs";
                document.getElementById("statusjson").innerText = text;
            })
            .catch(error => console.warn(error)),

        fetch("showrebootlog")
            .then((response) => {
                if (!response.ok || response.status !== 200) {
                    reject(`Error requsting reboot logs, RC: ${response.status}`);
                } else {
                    return response.text();
                }
            })
            .then((text) => {
                const { serverTime, upTime, bootTime } = findStartTime(text);
                const elem = document.getElementById("rebootlog");
                if (bootTime) {
                    let date = new Date();
                    date.setTime(bootTime);
                    elem.insertAdjacentHTML('beforebegin', `<pre style="margin: 0px; color:darkgoldenrod; font-size: 0.6em;">Server started at:  ${date.toUTCString()}</pre>`);
                    date.setTime(serverTime);
                    elem.insertAdjacentHTML('beforebegin', `<pre style="margin: 0px; color:darkgoldenrod; font-size: 0.6em;">Server shutdown at: ${date.toUTCString()}</pre>`);
                }
                if (upTime) {
                    elem.insertAdjacentHTML('beforebegin', `<pre style="margin: 0px; color:darkgoldenrod; font-size: 0.6em;">Server upTime:      ${msToTime(upTime)}</pre>`);
                }
                elem.innerText = text;
            })
            .catch(error => console.warn(error)),

        fetch("crashlog")
            .then((response) => {
                if (!response.ok || response.status !== 200) {
                    reject(`Error requsting crash logs, RC: ${response.status}`);
                } else {
                    return response.text();
                }
            })
            .then((text) => {
                const { serverTime, upTime, bootTime } = findStartTime(text);
                const elem = document.getElementById("crashlog-timestamps");
                if (bootTime) {
                    let date = new Date();
                    date.setTime(bootTime);
                    elem.insertAdjacentHTML('beforeend', `<pre style="margin: 0px; color:darkgoldenrod; font-size: 0.6em;">Server started at: ${date.toUTCString()}</pre>`);
                    date.setTime(serverTime);
                    elem.insertAdjacentHTML('beforeend', `<pre style="margin: 0px; color:darkgoldenrod; font-size: 0.6em;">Server crashed at: ${date.toUTCString()}</pre>`);
                }
                if (upTime) {
                    elem.insertAdjacentHTML('beforeend', `<pre style="margin: 0px; color:darkgoldenrod; font-size: 0.6em;">Server upTime:     ${msToTime(upTime)}</pre>`);
                }
                document.getElementById("crashlog").innerText = text;
            })
            .catch(error => console.warn(error)),
    ])
        .then((results) => {
            // Once all loaded reset the progress indicator
            loaderElem.style.visibility = "hidden";
            console.log("All logs loaded");
            //console.log(results);
        });
}

async function clearLog(reload) {
    // Erase current content
    document.getElementById("showlog").innerText = "";
    document.getElementById("showLogHeader").innerHTML = "";
    // Load logs
    if (reload) loadLogs();
}

async function clearCrashLog() {
    loaderElem.style.visibility = "visible";
    await fetch('clearcrashlog');
    document.getElementById("clearBtn").style.display = "none";
    if (msgJson) msgJson.crashCount = 0;
    document.getElementById("crashlog").innerText = "No crashes saved";
    document.getElementById("crashlog-timestamps").innerText = "";
    loaderElem.style.visibility = "hidden";
}
// Generate a UUID.  Cannot use crypto.randomUUID() because that will only run
// in a secure environment, which is not possible with ratgdo.
function uuidv4() {
    return "10000000-1000-4000-8000-100000000000".replace(/[018]/g, c =>
        (+c ^ crypto.getRandomValues(new Uint8Array(1))[0] & 15 >> +c / 4).toString(16)
    );
}