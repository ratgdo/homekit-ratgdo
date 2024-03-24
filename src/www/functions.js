/***********************************************************************
 * homekit-ratgdo web page javascript functions
 *
 * Copyright (c) 2023-24 David Kerr, https://github.com/dkerr64
 *
 */

// Global vars...
var serverStatus = {};    // object into which all server status is held.
var checkHeartbeat = undefined;   // setInterval for heartbeat timeout
var evtSource = undefined;// for Server Sent Events (SSE)

// convert miliseconds to dd:hh:mm:ss used to calculate server uptime
function msToTime(duration) {
    var milliseconds = Math.floor((duration % 1000) / 100),
        seconds = Math.floor((duration / 1000) % 60),
        minutes = Math.floor((duration / (1000 * 60)) % 60),
        hours = Math.floor((duration / (1000 * 60 * 60)) % 24),
        days = Math.floor((duration / (1000 * 60 * 60 * 24)));

    hours = (hours < 10) ? "0" + hours : hours;
    minutes = (minutes < 10) ? "0" + minutes : minutes;
    seconds = (seconds < 10) ? "0" + seconds : seconds;

    return days + ":" + hours + ":" + minutes + ":" + seconds;
}

// checkStatus is called once on page load to retrieve status from the server...
// and setInterval a timer that will refresh the data every 10 seconds
async function checkStatus() {
    if (checkHeartbeat) {
        clearInterval(checkHeartbeat);
        checkHeartbeat = undefined;
    }
    try {
        const response = await fetch("status.json")
            .catch((error) => {
                console.warn(`Promise rejection error fetching status from RATGDO, try again in 5 seconds: ${error}`);
                throw ("Promise rejection");
            });
        if (!response.ok || response.status !== 200) {
            console.warn(`Error RC ${response.status} fetching status from RATGDO, try again in 5 seconds.`);
            throw ("Error RC");
        }
        serverStatus = await response.json();
    }
    catch {
        setTimeout(checkStatus, 5000);
        return;
    }
    console.log(serverStatus);
    // Add letter 'v' to front of returned firmware version.
    // Hack because firmware uses v0.0.0 and 0.0.0 for different purposes.
    serverStatus.firmwareVersion = "v" + serverStatus.firmwareVersion;

    document.getElementById("devicename").innerHTML = serverStatus.deviceName;
    if (serverStatus.paired) {
        document.getElementById("unpair").value = "Un-pair HomeKit";
        document.getElementById("qrcode").style.display = "none";
        document.getElementById("re-pair-info").style.display = "inline-block";
    } else {
        document.getElementById("unpair").value = "Reset HomeKit";
        document.getElementById("re-pair-info").style.display = "none";
        document.getElementById("qrcode").style.display = "inline-block";
    }
    document.getElementById("uptime").innerHTML = msToTime(serverStatus.upTime);
    const date = new Date(Date.now() - serverStatus.upTime);
    document.getElementById("lastreboot").innerHTML = date.toLocaleString();
    document.getElementById("firmware").innerHTML = serverStatus.firmwareVersion;
    document.getElementById("firmware2").innerHTML = serverStatus.firmwareVersion;
    document.getElementById("ssid").innerHTML = serverStatus.wifiSSID;
    document.getElementById("macaddress").innerHTML = serverStatus.macAddress;
    document.getElementById("ipaddress").innerHTML = serverStatus.localIP;
    document.getElementById("netmask").innerHTML = serverStatus.subnetMask;
    document.getElementById("gateway").innerHTML = serverStatus.gatewayIP;
    document.getElementById("accessoryid").innerHTML = serverStatus.accessoryID;

    document.getElementById("gdosecuritytype").innerHTML = (serverStatus.GDOSecurityType == 1) ? "Sec+ " : "Sec+ 2.0";

    document.getElementById("doorstate").innerHTML = serverStatus.garageDoorState;
    document.getElementById("lockstate").innerHTML = serverStatus.garageLockState;
    document.getElementById("lighton").innerHTML = serverStatus.garageLightOn;
    document.getElementById("obstruction").innerHTML = serverStatus.garageObstructed;
    document.getElementById("motion").innerHTML = serverStatus.garageMotion;

    document.getElementById("newDevicename").placeholder = serverStatus.deviceName;
    document.getElementById("gdosec1").checked = (serverStatus.GDOSecurityType == 1) ? true : false;
    document.getElementById("gdosec2").checked = (serverStatus.GDOSecurityType == 2) ? true : false;
    document.getElementById("pwreq").checked = serverStatus.passwordRequired;
    document.getElementById("reboothours").value = serverStatus.rebootSeconds / 60 / 60;
    document.getElementById("freeheap").innerHTML = serverStatus.freeHeap;
    document.getElementById("minheap").innerHTML = serverStatus.minHeap;
    document.getElementById("wifiphymode0").checked = (serverStatus.wifiPhyMode == 0) ? true : false;
    document.getElementById("wifiphymode1").checked = (serverStatus.wifiPhyMode == 1) ? true : false;
    document.getElementById("wifiphymode2").checked = (serverStatus.wifiPhyMode == 2) ? true : false;
    document.getElementById("wifiphymode3").checked = (serverStatus.wifiPhyMode == 3) ? true : false;

    // Use Server Send Events to keep status up-to-date, 2 == CLOSED
    if (!evtSource || evtSource.readyState == 2) {
        let evtCount = 0;
        let evtLastCount = 0;
        const evtResponse = await fetch("rest/events/subscribe");
        if (evtResponse.status !== 200) {
            console.warn("Error registering for Server Send Events");
            return;
        }
        const evtUrl = await evtResponse.text();

        console.log(`Register for server sent events at ${evtUrl}`);
        evtSource = new EventSource(evtUrl);
        evtSource.addEventListener("message", (event) => {
            //console.log(`Message received: ${event.data}`);
            evtCount++;
            var msgJson = JSON.parse(event.data);
            serverStatus = { ...serverStatus, ...msgJson };
            // Update the HTML for those values that were present in the message...
            if (msgJson.hasOwnProperty("paired")) {
                if (serverStatus.paired) {
                    document.getElementById("unpair").value = "Un-pair HomeKit";
                    document.getElementById("qrcode").style.display = "none";
                    document.getElementById("re-pair-info").style.display = "inline-block";
                } else {
                    document.getElementById("unpair").value = "Reset HomeKit";
                    document.getElementById("re-pair-info").style.display = "none";
                    document.getElementById("qrcode").style.display = "inline-block";
                }
            }
            if (msgJson.hasOwnProperty("upTime")) document.getElementById("uptime").innerHTML = msToTime(serverStatus.upTime);
            if (msgJson.hasOwnProperty("garageDoorState")) document.getElementById("doorstate").innerHTML = serverStatus.garageDoorState;
            if (msgJson.hasOwnProperty("garageLockState")) document.getElementById("lockstate").innerHTML = serverStatus.garageLockState;
            if (msgJson.hasOwnProperty("garageLightOn")) document.getElementById("lighton").innerHTML = serverStatus.garageLightOn;
            if (msgJson.hasOwnProperty("garageObstructed")) document.getElementById("obstruction").innerHTML = serverStatus.garageObstructed;
            if (msgJson.hasOwnProperty("garageMotion")) document.getElementById("motion").innerHTML = serverStatus.garageMotion;
            if (msgJson.hasOwnProperty("freeHeap")) document.getElementById("freeheap").innerHTML = serverStatus.freeHeap;
            if (msgJson.hasOwnProperty("minHeap")) document.getElementById("minheap").innerHTML = serverStatus.minHeap;
        });
        evtSource.addEventListener("error", (event) => {
            // If an error occurs close the connection, then wait 5 seconds and try again.
            console.log(`SSE error occurred while attempting to connect to ${evtSource.url}`);
            evtSource.close();
            setTimeout(checkStatus, 5000);
        });
        checkHeartbeat = setInterval(() => {
            if (evtLastCount == evtCount) {
                // if no message received since last check then close connection and try again.
                console.log(`SSE timeout, no message received in 5 seconds. Last upTime: ${serverStatus.upTime} (${msToTime(serverStatus.upTime)})`);
                clearInterval(checkHeartbeat);
                checkHeartbeat = undefined;
                evtSource.close();
                setTimeout(checkStatus, 1000);
            }
            evtLastCount = evtCount;
        }, 5000);
    } else {
        console.log(`SSE already setup at ${evtSource.url}, State: ${evtSource.readyState}`);
    }
    return;
};

// Displays a series of dot-dot-dots into an element's innerHTML to give
// user some reassurance of activity.  Used during firmware update.
function dotDotDot(elem) {
    var i = 0;
    var dots = ".";
    return setInterval(() => {
        if (i++ % 20) {
            dots = dots + ".";
        } else {
            dots = ".";
        }
        elem.innerHTML = dots;
    }, 500);
}

async function checkVersion(progress) {
    const versionElem = document.getElementById("newversion");
    const versionElem2 = document.getElementById("newversion2");
    versionElem.innerHTML = "Checking";
    versionElem2.innerHTML = "Checking";
    const spanDots = document.getElementById(progress);
    const aniDots = dotDotDot(spanDots);
    const response = await fetch("https://api.github.com/repos/ratgdo/homekit-ratgdo/releases", {
        method: "GET",
        cache: "no-cache",
        redirect: "follow"
    });
    const releases = await response.json();
    if (response.status !== 200) {
        // We have probably hit the GitHub API rate limits (60 per hour for non-authenticated)
        versionElem.innerHTML = "";
        versionElem2.innerHTML = "";
        console.warn("Error retrieving status from GitHub" + releases.message);
        return;
    }
    // make sure we have newest release first
    const latest = releases.sort((a, b) => {
        return Date.parse(b.created_at) - Date.parse(a.created_at);
    })[0];
    serverStatus.latestVersion = latest;
    console.log("Newest version: " + latest.tag_name);
    const asset = latest.assets.find((obj) => {
        return (obj.content_type === "application/octet-stream") && (obj.name.startsWith("homekit-ratgdo"));
    });
    serverStatus.downloadURL = "https://ratgdo.github.io/homekit-ratgdo/firmware/" + asset.name;
    let msg = "You have newest release";
    if (serverStatus.firmwareVersion !== latest.tag_name) {
        // Newest version at GitHub is different from that installed
        msg = "Update available  (" + latest.tag_name + ")";
    }
    clearInterval(aniDots);
    spanDots.innerHTML = "";
    versionElem.innerHTML = msg;
    versionElem2.innerHTML = latest.tag_name;
}

// repurposes the myModal <div> to display a countdown timer
// from N seconds to zero, at end of which the page is reloaded.
// Used at end of firmware update or on reboot request.
function countdown(secs, msg) {
    // we are counting down to a reload... so clear heartbeat timeout check.
    clearInterval(checkHeartbeat);
    checkHeartbeat = undefined;
    const spanDots = document.getElementById("dotdot3");
    document.getElementById("modalTitle").innerHTML = "";
    document.getElementById("updateMsg").innerHTML = msg;
    if (document.getElementById("updateDialog")) {
        document.getElementById("updateDialog").style.display = "none";
        document.getElementById("modalClose").style.display = 'none';
    }
    document.getElementById("myModal").style.display = 'block';
    document.getElementById("updateDotDot").style.display = "block";
    spanDots.innerHTML = "";
    var seconds = secs;
    spanDots.innerHTML = seconds;
    var countdown = setInterval(() => {
        if (seconds-- === 0) {
            clearInterval(countdown);
            location.href = "/";
            return;
        } else {
            spanDots.innerHTML = seconds;
        }
    }, 1000);
}

// Handles request to update server firmware from either GitHub (default) or from
// a user provided file.
async function firmwareUpdate(github = true) {
    var showRebootMsg = false;
    const spanDots = document.getElementById("dotdot3");
    const spanMsg = document.getElementById("updateMsg");
    const aniDots = dotDotDot(spanDots);
    try {
        document.getElementById("updateDialog").style.display = "none";
        document.getElementById("updateDotDot").style.display = "block";
        if (github) {
            if (!serverStatus.latestVersion) {
                console.log("Cannot download firmware, latest version unknown");
                alert("Firmware version at GitHub is unknown, cannot update directly from GitHub.");
                return;
            }
            console.log("Download firmware from: " + serverStatus.downloadURL);
            let response = await fetch(serverStatus.downloadURL, {
                method: "GET",
                cache: "no-cache",
                redirect: "follow",
                headers: {
                    "Accept": "application/octet-stream",
                },
            });
            const blob = await response.blob();
            console.log("Download complete, size: " + blob.size);
            const formData = new FormData();
            formData.append("content", blob);
            response = await fetch("update", {
                method: "POST",
                body: formData,
            });
        } else {
            const inputElem = document.querySelector('input[type="file"]');
            const formData = new FormData();
            if (inputElem.files.length > 0) {
                console.log("Uploading file: " + inputElem.files[0]);
                formData.append("file", inputElem.files[0]);
                response = await fetch("update", {
                    method: "POST",
                    body: formData,
                });
            } else {
                console.log("No file name provided");
                alert("You must select a file to upload.");
                return;
            }
        }
        showRebootMsg = true;
        console.log("Upload complete");
    }
    finally {
        clearInterval(aniDots);
        if (showRebootMsg) {
            countdown(30, "Update complete, RATGDO device rebooting...&nbsp;");
        } else {
            document.getElementById("updateDotDot").style.display = "none";
            document.getElementById("updateDialog").style.display = "block";
        }
    }
}

async function rebootRATGDO() {
    var response = await fetch("reboot", {
        method: "POST",
    });
    if (response.status !== 200) {
        console.warn("Error attempting to reboot RATGDO");
        return;
    }
    countdown(30, "RATGDO device rebooting...&nbsp;");
}

async function unpairRATGDO() {
    var response = await fetch("reset", {
        method: "POST",
    });
    if (response.status !== 200) {
        console.warn("Error attempting to unpair and reboot RATGDO");
        return;
    }
    countdown(30, "RATGO un-pairing and rebooting...&nbsp;");
}

async function checkAuth() {
    auth = false;
    var response = await fetch("auth", {
        method: "GET",
    });
    if (response.status == 200) {
        auth = true;
    }
    else if (response.status == 401) {
        console.warn("Not Authenticated");
    }
    return auth;
}

async function setGDO(...args) {
    try {
        // check if authenticated, before post to setgdo, prevents timeout of dialog due to AbortSignal
        if (!await checkAuth()) {
            return;
        }
        const formData = new FormData();
        for (let i = 0; i < args.length; i = i + 2) {
            if (args[i + 1].length > 0) {
                formData.append(args[i], args[i + 1]);
            }
        }
        var response = await fetch("setgdo", {
            method: "POST",
            body: formData,
            signal: AbortSignal.timeout(2000),
        });
        if (response.status !== 200) {
            console.warn("Error setting RATGDO state");
            return;
        }
    }
    catch (err) {
        if (err.name === "TimeoutError") {
            console.error("Timeout: It took more than 5 seconds to get the result!");
        } else if (err.name === "AbortError") {
            console.error("Fetch aborted by user action (browser stop button, closing tab, etc.");
        } else if (err.name === "TypeError") {
            console.error("AbortSignal.timeout() method is not supported");
        } else {
            // A network error, or some other problem.
            console.error(`Error: type: ${err.name}, message: ${err.message}`);
        }
    }
}

async function changePassword() {
    if (newPW.value === "") {
        alert("New password cannot be blank");
        return;
    }
    if (newPW.value !== confirmPW.value) {
        alert("Passwords do not match");
        return;
    }
    const www_username = "admin";
    const www_realm = "RATGDO Login Required";
    passwordHash = MD5(www_username + ":" + www_realm + ":" + newPW.value);
    console.log("Set new credentials to: " + passwordHash);
    await setGDO("credentials", passwordHash);
    clearInterval(checkHeartbeat);
    checkHeartbeat = undefined;
    // On success, go to home page.
    // User will have to re-authenticate to get back to settings.
    location.href = "/";
    return;
}

async function saveSettings() {
    let gdoSec = (document.getElementById("gdosec1").checked) ? '1' : '2';
    console.log("Set GDO security type to: " + gdoSec);
    let pwReq = (document.getElementById("pwreq").checked) ? '1' : '0';
    console.log("Set GDO Web Password required to: " + pwReq);
    let rebootHours = Math.max(Math.min(parseInt(document.getElementById("reboothours").value), 72), 0);
    if (isNaN(rebootHours)) rebootHours = 0;
    console.log("Set GDO Reboot Every: " + (rebootHours * 60 * 60) + " seconds");
    let newDeviceName = document.getElementById("newDevicename").value.substring(0, 30);
    console.log("Set device name to: " + newDeviceName);
    let wifiPhyMode = (document.getElementById("wifiphymode3").checked) ? '3'
        : (document.getElementById("wifiphymode2").checked) ? '2'
            : (document.getElementById("wifiphymode1").checked) ? '1'
                : '0';
    console.log("Set GDO WiFi version to: " + wifiPhyMode);

    await setGDO("gdoSecurity", gdoSec,
        "passwordRequired", pwReq,
        "rebootSeconds", rebootHours * 60 * 60,
        "newDeviceName", newDeviceName,
        "wifiPhyMode", wifiPhyMode);
    countdown(30, "Settings saved, RATGDO device rebooting...&nbsp;");
    return;
}

// Functions to support mobile device swipe-down to reload...
let pStart = { x: 0, y: 0 };
let pStop = { x: 0, y: 0 };
function swipeStart(e) {
    if (typeof e['targetTouches'] !== "undefined") {
        const touch = e.targetTouches[0];
        pStart.x = touch.screenX;
        pStart.y = touch.screenY;
    } else {
        pStart.x = e.screenX;
        pStart.y = e.screenY;
    }
}
function swipeEnd(e) {
    if (typeof e['changedTouches'] !== "undefined") {
        const touch = e.changedTouches[0];
        pStop.x = touch.screenX;
        pStop.y = touch.screenY;
    } else {
        pStop.x = e.screenX;
        pStop.y = e.screenY;
    }
    swipeCheck();
}
function swipeCheck() {
    const changeY = pStart.y - pStop.y;
    const changeX = pStart.x - pStop.x;
    if (isPullDown(changeY, changeX)) {
        // alert('Swipe Down!');
        location.reload();
    }
}
function isPullDown(dY, dX) {
    // methods of checking slope, length, direction of line created by swipe action 
    return dY < 0 && (
        (Math.abs(dX) <= 100 && Math.abs(dY) >= 300)
        || (Math.abs(dX) / Math.abs(dY) <= 0.3 && dY >= 60)
    );
}

// MD5 Hash function from
// https://stackoverflow.com/questions/14733374/how-to-generate-an-md5-hash-from-a-string-in-javascript-node-js
// We use this to obfuscate a new password/credentials when sent to server so that
// it is not obvious in the network transmission
var MD5 = function (d) { var r = M(V(Y(X(d), 8 * d.length))); return r.toLowerCase(); }; function M(d) { for (var _, m = "0123456789ABCDEF", f = "", r = 0; r < d.length; r++)_ = d.charCodeAt(r), f += m.charAt(_ >>> 4 & 15) + m.charAt(15 & _); return f; } function X(d) { for (var _ = Array(d.length >> 2), m = 0; m < _.length; m++)_[m] = 0; for (m = 0; m < 8 * d.length; m += 8)_[m >> 5] |= (255 & d.charCodeAt(m / 8)) << m % 32; return _; } function V(d) { for (var _ = "", m = 0; m < 32 * d.length; m += 8)_ += String.fromCharCode(d[m >> 5] >>> m % 32 & 255); return _; } function Y(d, _) { d[_ >> 5] |= 128 << _ % 32, d[14 + (_ + 64 >>> 9 << 4)] = _; for (var m = 1732584193, f = -271733879, r = -1732584194, i = 271733878, n = 0; n < d.length; n += 16) { var h = m, t = f, g = r, e = i; f = md5_ii(f = md5_ii(f = md5_ii(f = md5_ii(f = md5_hh(f = md5_hh(f = md5_hh(f = md5_hh(f = md5_gg(f = md5_gg(f = md5_gg(f = md5_gg(f = md5_ff(f = md5_ff(f = md5_ff(f = md5_ff(f, r = md5_ff(r, i = md5_ff(i, m = md5_ff(m, f, r, i, d[n + 0], 7, -680876936), f, r, d[n + 1], 12, -389564586), m, f, d[n + 2], 17, 606105819), i, m, d[n + 3], 22, -1044525330), r = md5_ff(r, i = md5_ff(i, m = md5_ff(m, f, r, i, d[n + 4], 7, -176418897), f, r, d[n + 5], 12, 1200080426), m, f, d[n + 6], 17, -1473231341), i, m, d[n + 7], 22, -45705983), r = md5_ff(r, i = md5_ff(i, m = md5_ff(m, f, r, i, d[n + 8], 7, 1770035416), f, r, d[n + 9], 12, -1958414417), m, f, d[n + 10], 17, -42063), i, m, d[n + 11], 22, -1990404162), r = md5_ff(r, i = md5_ff(i, m = md5_ff(m, f, r, i, d[n + 12], 7, 1804603682), f, r, d[n + 13], 12, -40341101), m, f, d[n + 14], 17, -1502002290), i, m, d[n + 15], 22, 1236535329), r = md5_gg(r, i = md5_gg(i, m = md5_gg(m, f, r, i, d[n + 1], 5, -165796510), f, r, d[n + 6], 9, -1069501632), m, f, d[n + 11], 14, 643717713), i, m, d[n + 0], 20, -373897302), r = md5_gg(r, i = md5_gg(i, m = md5_gg(m, f, r, i, d[n + 5], 5, -701558691), f, r, d[n + 10], 9, 38016083), m, f, d[n + 15], 14, -660478335), i, m, d[n + 4], 20, -405537848), r = md5_gg(r, i = md5_gg(i, m = md5_gg(m, f, r, i, d[n + 9], 5, 568446438), f, r, d[n + 14], 9, -1019803690), m, f, d[n + 3], 14, -187363961), i, m, d[n + 8], 20, 1163531501), r = md5_gg(r, i = md5_gg(i, m = md5_gg(m, f, r, i, d[n + 13], 5, -1444681467), f, r, d[n + 2], 9, -51403784), m, f, d[n + 7], 14, 1735328473), i, m, d[n + 12], 20, -1926607734), r = md5_hh(r, i = md5_hh(i, m = md5_hh(m, f, r, i, d[n + 5], 4, -378558), f, r, d[n + 8], 11, -2022574463), m, f, d[n + 11], 16, 1839030562), i, m, d[n + 14], 23, -35309556), r = md5_hh(r, i = md5_hh(i, m = md5_hh(m, f, r, i, d[n + 1], 4, -1530992060), f, r, d[n + 4], 11, 1272893353), m, f, d[n + 7], 16, -155497632), i, m, d[n + 10], 23, -1094730640), r = md5_hh(r, i = md5_hh(i, m = md5_hh(m, f, r, i, d[n + 13], 4, 681279174), f, r, d[n + 0], 11, -358537222), m, f, d[n + 3], 16, -722521979), i, m, d[n + 6], 23, 76029189), r = md5_hh(r, i = md5_hh(i, m = md5_hh(m, f, r, i, d[n + 9], 4, -640364487), f, r, d[n + 12], 11, -421815835), m, f, d[n + 15], 16, 530742520), i, m, d[n + 2], 23, -995338651), r = md5_ii(r, i = md5_ii(i, m = md5_ii(m, f, r, i, d[n + 0], 6, -198630844), f, r, d[n + 7], 10, 1126891415), m, f, d[n + 14], 15, -1416354905), i, m, d[n + 5], 21, -57434055), r = md5_ii(r, i = md5_ii(i, m = md5_ii(m, f, r, i, d[n + 12], 6, 1700485571), f, r, d[n + 3], 10, -1894986606), m, f, d[n + 10], 15, -1051523), i, m, d[n + 1], 21, -2054922799), r = md5_ii(r, i = md5_ii(i, m = md5_ii(m, f, r, i, d[n + 8], 6, 1873313359), f, r, d[n + 15], 10, -30611744), m, f, d[n + 6], 15, -1560198380), i, m, d[n + 13], 21, 1309151649), r = md5_ii(r, i = md5_ii(i, m = md5_ii(m, f, r, i, d[n + 4], 6, -145523070), f, r, d[n + 11], 10, -1120210379), m, f, d[n + 2], 15, 718787259), i, m, d[n + 9], 21, -343485551), m = safe_add(m, h), f = safe_add(f, t), r = safe_add(r, g), i = safe_add(i, e); } return Array(m, f, r, i); } function md5_cmn(d, _, m, f, r, i) { return safe_add(bit_rol(safe_add(safe_add(_, d), safe_add(f, i)), r), m); } function md5_ff(d, _, m, f, r, i, n) { return md5_cmn(_ & m | ~_ & f, d, _, r, i, n); } function md5_gg(d, _, m, f, r, i, n) { return md5_cmn(_ & f | m & ~f, d, _, r, i, n); } function md5_hh(d, _, m, f, r, i, n) { return md5_cmn(_ ^ m ^ f, d, _, r, i, n); } function md5_ii(d, _, m, f, r, i, n) { return md5_cmn(m ^ (_ | ~f), d, _, r, i, n); } function safe_add(d, _) { var m = (65535 & d) + (65535 & _); return (d >> 16) + (_ >> 16) + (m >> 16) << 16 | 65535 & m; } function bit_rol(d, _) { return d << _ | d >>> 32 - _; }
