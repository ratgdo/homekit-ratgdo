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
var sysLogLoaded = false;
var tmpLogMsgs = [];

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
    sysLogLoaded = false;
    tmpLogMsgs.length = 0;
    // Load all the logs in parallel, showing progress indicator while we do...
    loaderElem.style.visibility = "visible";
    console.log("Subscribe to Server Sent Events");
    fetch("rest/events/subscribe?id=" + clientUUID + "&log=1&heartbeat=0")
        .then((response) => {
            if (!response.ok || response.status !== 200) {
                reject(`Error registering for Server Sent Events, RC: ${response.status}`);
            } else {
                return response.text();
            }
        })
        .then((text) => {
            const evtUrl = text + '?id=' + clientUUID;
            console.log(`Register for Server Sent Events at ${evtUrl}`);
            evtSource = new EventSource(evtUrl);
            evtSource.onopen = () => {
                console.log("Load each log page");
                loadLogPages();
            };
            evtSource.addEventListener("logger", (event) => {
                let divElem = document.getElementById("logTab");
                let scroll = (divElem.scrollHeight - divElem.scrollTop - divElem.clientHeight) < 10;
                document.getElementById("showlog").insertAdjacentText('beforeend', event.data + "\n");
                if (!sysLogLoaded) tmpLogMsgs.push(event.data);
                // Only scroll the page if we are already at bottom of the page
                if (scroll) divElem.scrollTop = divElem.scrollHeight;
            });
            evtSource.addEventListener("error", (event) => {
                // If an error occurs close the connection.
                console.log(`SSE error occurred while attempting to connect to ${evtSource.url}`);
                evtSource.close();
            });
        })
        .catch((error) => {
            console.warn(`Failed to register for Server Sent Events: ${error}`);
        });
}

async function loadLogPages() {
    // Load the pages in background
    Promise.allSettled([

        fetch("showlog")
            .then((response) => {
                if (!response.ok || response.status !== 200) {
                    reject(`Error requesting logs, RC: ${response.status}`);
                } else {
                    return response.text();
                }
            })
            .then((text) => {
                sysLogLoaded = true;
                // reduce newlines down to single \n
                text = text.replaceAll('\r\n', '\n');
                while (line = tmpLogMsgs.pop()) {
                    console.log(`Remove dup: ${line}`);
                    text = text.replace(line + '\n', '');
                }
                document.getElementById("showlog").insertAdjacentText('afterbegin', text);
                let divElem = document.getElementById("logTab");
                // Scroll to the bottom
                divElem.scrollTop = divElem.scrollHeight;
            })
            .catch(error => console.warn(error)),

        fetch("status.json")
            .then((response) => {
                if (!response.ok || response.status !== 200) {
                    reject(`Error requesting status.json, RC: ${response.status}`);
                } else {
                    return response.text();
                }
            })
            .then((text) => {
                msgJson = JSON.parse(text);
                document.getElementById("deviceName").innerHTML = msgJson.deviceName;
                document.title = msgJson.deviceName;
                document.getElementById("statusjson").innerText = text;
            })
            .catch(error => console.warn(error)),

        fetch("showrebootlog")
            .then((response) => {
                if (!response.ok || response.status !== 200) {
                    reject(`Error requesting reboot logs, RC: ${response.status}`);
                } else {
                    return response.text();
                }
            })
            .then((text) => {
                document.getElementById("rebootlog").innerText = text;
            })
            .catch(error => console.warn(error)),

        fetch("crashlog")
            .then((response) => {
                if (!response.ok || response.status !== 200) {
                    reject(`Error requesting crash logs, RC: ${response.status}`);
                } else {
                    return response.text();
                }
            })
            .then((text) => {
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
    loaderElem.style.visibility = "hidden";
}
// Generate a UUID.  Cannot use crypto.randomUUID() because that will only run
// in a secure environment, which is not possible with ratgdo.
function uuidv4() {
    return "10000000-1000-4000-8000-100000000000".replace(/[018]/g, c =>
        (+c ^ crypto.getRandomValues(new Uint8Array(1))[0] & 15 >> +c / 4).toString(16)
    );
}
