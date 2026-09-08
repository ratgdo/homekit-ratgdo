/***********************************************************************
 * homekit-ratgdo logger web page javascript functions
 *
 * Copyright (c) 2024-25 David Kerr, https://github.com/dkerr64
 *
 */

// Global vars...
var evtSource = undefined;      // for Server Sent Events (SSE)
var serverStatus = undefined;   // for status
const clientUUID = uuidv4();    // uniquely identify this session
var sysLogLoaded = false;
var tmpLogMsgs = [];

var passwordHash = undefined;
const www_realm = "RATGDO Login Required";

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

function promptPassword() {
    if (serverStatus?.passwordRequired && passwordHash === undefined) {
        let password = prompt("Please enter the password:");
        if (password === null) {
            console.warn("User cancelled password prompt");
            return false;
        }
        // MD5() function expects a Uint8Array typed ArrayBuffer...
        passwordHash = MD5((new TextEncoder).encode(serverStatus.userName + ":" + www_realm + ":" + password));
    }
    return true;
}

async function checkAuth(loader = true) {
    auth = false;
    if (promptPassword()) {
        if (loader) loaderElem.style.visibility = "visible";
        let response = await fetch("auth", { method: "GET", headers: { 'X-API-Key': passwordHash } });
        if (loader) loaderElem.style.visibility = "hidden";
        // Give browser a moment to actually hide the spinner...
        await new Promise(r => setTimeout(r, 50));
        if (response.status == 200) {
            auth = true;
        }
        else if (response.status == 401) {
            console.warn("401 Not Authenticated");
        }
        else if (response.status == 403) {
            console.warn("403 Forbidden, authentication failed");
            passwordHash = undefined;
        }
        else {
            console.warn(`Unexpected response from server: ${response.status}`);
        }
    }
    return auth;
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
        if (serverStatus?.crashCount != 0) {
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
                serverStatus = JSON.parse(text);
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
    // Fetch status from the server... we need it for the username and to check if authentication is required
    let response = await fetch("status.json");
    if (!response.ok || response.status !== 200) {
        reject(`Error requsting status.json, RC: ${response.status}`);
        loaderElem.style.visibility = "hidden";
        console.error(`Error requesting status.json, RC: ${response.status}`);
        return false;
    }
    serverStatus = JSON.parse(await response.text());
    // check if authenticated, before loading logs
    if (!await checkAuth(false)) {
        loaderElem.style.visibility = "hidden";
        console.warn("Authentication failed in loadLogs");
        return false;
    }
    else {
        console.log("Authentication successful in loadLogs");
    }
    console.log("Subscribe to Server Sent Events");
    fetch("rest/events/subscribe?id=" + clientUUID + "&log=1&heartbeat=0", { method: "GET", headers: { 'X-API-Key': passwordHash } })
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

        fetch("showlog", { method: "GET", headers: { 'X-API-Key': passwordHash } })
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
                serverStatus = JSON.parse(text);
                document.getElementById("deviceName").textContent = serverStatus.deviceName;
                document.title = serverStatus.deviceName;
                document.getElementById("statusjson").innerText = text;
            })
            .catch(error => console.warn(error)),

        fetch("showrebootlog", { method: "GET", headers: { 'X-API-Key': passwordHash } })
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

        fetch("crashlog", { method: "GET", headers: { 'X-API-Key': passwordHash } })
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
    await fetch('clearcrashlog', { method: "GET", headers: { 'X-API-Key': passwordHash } });
    document.getElementById("clearBtn").style.display = "none";
    if (serverStatus) serverStatus.crashCount = 0;
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

// MD5 Hash function from
// https://stackoverflow.com/questions/14733374/how-to-generate-an-md5-hash-from-a-string-in-javascript-node-js
// We use this to obfuscate a new password/credentials when sent to server so that
// it is not obvious in the network transmission
//
// Note... this function has been changed from the original to work on an ArrayBuffer typed to Uint8Array.
// Strings must be encoded into such an array before calling this function.
var MD5 = function (d) { var r = M(V(Y(X(d), 8 * d.length))); return r.toLowerCase(); }; function M(d) { for (var _, m = "0123456789ABCDEF", f = "", r = 0; r < d.length; r++)_ = d[r], f += m.charAt(_ >>> 4 & 15) + m.charAt(15 & _); return f; } function X(d) { for (var _ = Array(d.length >> 2), m = 0; m < _.length; m++)_[m] = 0; for (m = 0; m < 8 * d.length; m += 8)_[m >> 5] |= (255 & d[m / 8]) << m % 32; return _; } function V(d) { for (var _ = Array(), m = 0; m < 32 * d.length; m += 8)_.push(d[m >> 5] >>> m % 32 & 255); return _; } function Y(d, _) { d[_ >> 5] |= 128 << _ % 32, d[14 + (_ + 64 >>> 9 << 4)] = _; for (var m = 1732584193, f = -271733879, r = -1732584194, i = 271733878, n = 0; n < d.length; n += 16) { var h = m, t = f, g = r, e = i; f = md5_ii(f = md5_ii(f = md5_ii(f = md5_ii(f = md5_hh(f = md5_hh(f = md5_hh(f = md5_hh(f = md5_gg(f = md5_gg(f = md5_gg(f = md5_gg(f = md5_ff(f = md5_ff(f = md5_ff(f = md5_ff(f, r = md5_ff(r, i = md5_ff(i, m = md5_ff(m, f, r, i, d[n + 0], 7, -680876936), f, r, d[n + 1], 12, -389564586), m, f, d[n + 2], 17, 606105819), i, m, d[n + 3], 22, -1044525330), r = md5_ff(r, i = md5_ff(i, m = md5_ff(m, f, r, i, d[n + 4], 7, -176418897), f, r, d[n + 5], 12, 1200080426), m, f, d[n + 6], 17, -1473231341), i, m, d[n + 7], 22, -45705983), r = md5_ff(r, i = md5_ff(i, m = md5_ff(m, f, r, i, d[n + 8], 7, 1770035416), f, r, d[n + 9], 12, -1958414417), m, f, d[n + 10], 17, -42063), i, m, d[n + 11], 22, -1990404162), r = md5_ff(r, i = md5_ff(i, m = md5_ff(m, f, r, i, d[n + 12], 7, 1804603682), f, r, d[n + 13], 12, -40341101), m, f, d[n + 14], 17, -1502002290), i, m, d[n + 15], 22, 1236535329), r = md5_gg(r, i = md5_gg(i, m = md5_gg(m, f, r, i, d[n + 1], 5, -165796510), f, r, d[n + 6], 9, -1069501632), m, f, d[n + 11], 14, 643717713), i, m, d[n + 0], 20, -373897302), r = md5_gg(r, i = md5_gg(i, m = md5_gg(m, f, r, i, d[n + 5], 5, -701558691), f, r, d[n + 10], 9, 38016083), m, f, d[n + 15], 14, -660478335), i, m, d[n + 4], 20, -405537848), r = md5_gg(r, i = md5_gg(i, m = md5_gg(m, f, r, i, d[n + 9], 5, 568446438), f, r, d[n + 14], 9, -1019803690), m, f, d[n + 3], 14, -187363961), i, m, d[n + 8], 20, 1163531501), r = md5_gg(r, i = md5_gg(i, m = md5_gg(m, f, r, i, d[n + 13], 5, -1444681467), f, r, d[n + 2], 9, -51403784), m, f, d[n + 7], 14, 1735328473), i, m, d[n + 12], 20, -1926607734), r = md5_hh(r, i = md5_hh(i, m = md5_hh(m, f, r, i, d[n + 5], 4, -378558), f, r, d[n + 8], 11, -2022574463), m, f, d[n + 11], 16, 1839030562), i, m, d[n + 14], 23, -35309556), r = md5_hh(r, i = md5_hh(i, m = md5_hh(m, f, r, i, d[n + 1], 4, -1530992060), f, r, d[n + 4], 11, 1272893353), m, f, d[n + 7], 16, -155497632), i, m, d[n + 10], 23, -1094730640), r = md5_hh(r, i = md5_hh(i, m = md5_hh(m, f, r, i, d[n + 13], 4, 681279174), f, r, d[n + 0], 11, -358537222), m, f, d[n + 3], 16, -722521979), i, m, d[n + 6], 23, 76029189), r = md5_hh(r, i = md5_hh(i, m = md5_hh(m, f, r, i, d[n + 9], 4, -640364487), f, r, d[n + 12], 11, -421815835), m, f, d[n + 15], 16, 530742520), i, m, d[n + 2], 23, -995338651), r = md5_ii(r, i = md5_ii(i, m = md5_ii(m, f, r, i, d[n + 0], 6, -198630844), f, r, d[n + 7], 10, 1126891415), m, f, d[n + 14], 15, -1416354905), i, m, d[n + 5], 21, -57434055), r = md5_ii(r, i = md5_ii(i, m = md5_ii(m, f, r, i, d[n + 12], 6, 1700485571), f, r, d[n + 3], 10, -1894986606), m, f, d[n + 10], 15, -1051523), i, m, d[n + 1], 21, -2054922799), r = md5_ii(r, i = md5_ii(i, m = md5_ii(m, f, r, i, d[n + 8], 6, 1873313359), f, r, d[n + 15], 10, -30611744), m, f, d[n + 6], 15, -1560198380), i, m, d[n + 13], 21, 1309151649), r = md5_ii(r, i = md5_ii(i, m = md5_ii(m, f, r, i, d[n + 4], 6, -145523070), f, r, d[n + 11], 10, -1120210379), m, f, d[n + 2], 15, 718787259), i, m, d[n + 9], 21, -343485551), m = safe_add(m, h), f = safe_add(f, t), r = safe_add(r, g), i = safe_add(i, e); } return Array(m, f, r, i); } function md5_cmn(d, _, m, f, r, i) { return safe_add(bit_rol(safe_add(safe_add(_, d), safe_add(f, i)), r), m); } function md5_ff(d, _, m, f, r, i, n) { return md5_cmn(_ & m | ~_ & f, d, _, r, i, n); } function md5_gg(d, _, m, f, r, i, n) { return md5_cmn(_ & f | m & ~f, d, _, r, i, n); } function md5_hh(d, _, m, f, r, i, n) { return md5_cmn(_ ^ m ^ f, d, _, r, i, n); } function md5_ii(d, _, m, f, r, i, n) { return md5_cmn(m ^ (_ | ~f), d, _, r, i, n); } function safe_add(d, _) { var m = (65535 & d) + (65535 & _); return (d >> 16) + (_ >> 16) + (m >> 16) << 16 | 65535 & m; } function bit_rol(d, _) { return d << _ | d >>> 32 - _; }
