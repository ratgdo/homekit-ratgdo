/***********************************************************************
 * homekit-ratgdo web page javascript functions
 *
 * Copyright (c) 2023-24 David Kerr, https://github.com/dkerr64
 *
 */

// Global vars...
var serverStatus = {};          // object into which all server status is held.
var checkHeartbeat = undefined; // setTimeout for heartbeat timeout
var evtSource = undefined;      // for Server Sent Events (SSE)
var delayStatusFn = [];         // to keep track of possible checkStatus timeouts
const clientUUID = uuidv4();    // uniquely identify this session
const rebootSeconds = 15;       // How long to wait before reloading page after reboot

// https://stackoverflow.com/questions/7995752/detect-desktop-browser-not-mobile-with-javascript
// const isTouchDevice = function () { return 'ontouchstart' in window || 'onmsgesturechange' in window; };
// const isDesktop = window.screenX != 0 && !isTouchDevice() ? true : false;

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

// Update all elements on HTML page to reflect status
function setElementsFromStatus(status) {
    let date = new Date();
    for (const [key, value] of Object.entries(status)) {
        switch (key) {
            case "paired":
                if (value) {
                    document.getElementById("unpair").value = "Un-pair HomeKit";
                    document.getElementById("qrcode").style.display = "none";
                    document.getElementById("re-pair-info").style.display = "inline-block";
                } else {
                    document.getElementById("unpair").value = "Reset HomeKit";
                    document.getElementById("re-pair-info").style.display = "none";
                    document.getElementById("qrcode").style.display = "inline-block";
                }
                break;
            case "upTime":
                document.getElementById(key).innerHTML = msToTime(value);
                date.setTime(Date.now() - value);
                document.getElementById("lastRebootAt").innerHTML = date.toLocaleString();
                break;
            case "GDOSecurityType":
                document.getElementById(key).innerHTML = (value == 1) ? "Sec+ " : "Sec+ 2.0";
                document.getElementById("gdosec1").checked = (value == 1) ? true : false;
                document.getElementById("gdosec2").checked = (value == 2) ? true : false;
                break;
            case "deviceName":
                document.getElementById(key).innerHTML = value;
                document.title = value;
                document.getElementById("newDeviceName").placeholder = value;
                break;
            case "userName":
                document.getElementById("newUserName").placeholder = value;
                break;
            case "passwordRequired":
                document.getElementById("pwreq").checked = value;
                break;
            case "LEDidle":
                document.getElementById("LEDidle").checked = (value == 1);
                break;
            case "rebootSeconds":
                document.getElementById("rebootHours").value = value / 60 / 60;
                break;
            case "TTCseconds":
                document.getElementById(key).value = value;
                document.getElementById("TTCsecondsValue").innerHTML = value;
                break;
            case "firmwareVersion":
                document.getElementById(key).innerHTML = value;
                document.getElementById("firmwareVersion2").innerHTML = value;
                break;
            case "wifiPhyMode":
                document.getElementById("wifiPhyMode0").checked = (value == 0) ? true : false;
                document.getElementById("wifiPhyMode1").checked = (value == 1) ? true : false;
                document.getElementById("wifiPhyMode2").checked = (value == 2) ? true : false;
                document.getElementById("wifiPhyMode3").checked = (value == 3) ? true : false;
                break;
            case "wifiPower":
                document.getElementById(key).value = value;
                document.getElementById("wifiPowerValue").innerHTML = value;
                break;
            case "localIP":
                document.getElementById(key).innerHTML = value;
                document.getElementById("IPaddress").placeholder = value;
                break;
            case "subnetMask":
                document.getElementById(key).innerHTML = value;
                document.getElementById("IPnetmask").placeholder = value;
                break;
            case "gatewayIP":
                document.getElementById(key).innerHTML = value;
                document.getElementById("IPgateway").placeholder = value;
                break;
            case "nameserverIP":
                document.getElementById("IPnameserver").placeholder = value;
                break;
            case "staticIP":
                document.getElementById(key).checked = value;
                document.getElementById("staticIPtable").style.display = (value) ? "table" : "none";
                break;
            case "syslogIP":
                document.getElementById(key).innerHTML = value;
                document.getElementById("syslogIP").placeholder = value;
                break;
            case "syslogEn":
                document.getElementById(key).checked = value;
                document.getElementById("syslogTable").style.display = (value) ? "table" : "none";
                break;
            case "enableNTP":
                document.getElementById(key).checked = value;
                break;
            case "lastDoorUpdateAt":
                date.setTime(Date.now() - value);
                document.getElementById(key).innerHTML = (document.getElementById("lastRebootAt").innerHTML == date.toLocaleString()) ? "Unknown" : date.toLocaleString();
                break;
            case "serverTime":
                date.setTime(value * 1000);
                console.log(`Server time: ${date.toUTCString()}`);
                break;
            case "checkFlashCRC":
                if (!value) {
                    console.warn("WARNING: Server checkFlashCRC() failed. Flash new firmware by USB cable to recover.");
                    document.getElementById("checkFlashCRC").style.display = "initial";
                }
                break;
            case "motionTriggers":
                setMotionTriggers(value);
                break;
            case "freeIramHeap":
                // Unused... remove this case statement when/if we add to html.
                break;
            default:
                try {
                    document.getElementById(key).innerHTML = value;
                } catch (error) {
                    console.error(`Server sent unrecognized status key: ${key}`);
                }
        }
    }
}

// checkStatus is called once on page load to retrieve status from the server...
// and setInterval a timer that will refresh the data every 10 seconds
async function checkStatus() {
    // clean up any awaiting timeouts...
    clearTimeout(checkHeartbeat);
    while (delayStatusFn.length) clearTimeout(delayStatusFn.pop());
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
        delayStatusFn.push(setTimeout(checkStatus, 5000));
        return;
    }
    console.log(serverStatus);
    // Add letter 'v' to front of returned firmware version.
    // Hack because firmware uses v0.0.0 and 0.0.0 for different purposes.
    serverStatus.firmwareVersion = "v" + serverStatus.firmwareVersion;

    setElementsFromStatus(serverStatus);
    // Use Server Sent Events to keep status up-to-date, 2 == CLOSED
    if (!evtSource || evtSource.readyState == 2) {
        const evtResponse = await fetch("rest/events/subscribe?id=" + clientUUID);
        if (evtResponse.status !== 200) {
            console.warn("Error registering for Server Sent Events");
            return;
        }
        const evtUrl = (await evtResponse.text()) + '?id=' + clientUUID;

        console.log(`Register for server sent events at ${evtUrl}`);
        evtSource = new EventSource(evtUrl);
        evtSource.addEventListener("message", (event) => {
            //console.log(`Message received: ${event.data}`);
            clearTimeout(checkHeartbeat);
            checkHeartbeat = setTimeout(() => {
                // if no message received since last check then close connection and try again.
                console.log(`SSE timeout, no message received in 30 seconds. Last upTime: ${serverStatus.upTime} (${msToTime(serverStatus.upTime)})`);
                evtSource.close();
                delayStatusFn.push(setTimeout(checkStatus, 1000));
            }, 30000);
            var msgJson = JSON.parse(event.data);
            serverStatus = { ...serverStatus, ...msgJson };
            // Update the HTML for those values that were present in the message...
            setElementsFromStatus(msgJson);
        });
        evtSource.addEventListener("logger", (event) => {
            console.log(event.data);
        });
        evtSource.addEventListener("uploadStatus", (event) => {
            //console.log(event.data);
            let msgJson = JSON.parse(event.data);
            let spanPercent = document.getElementById("updatePercent");
            spanPercent.style.display = 'initial';
            spanPercent.innerHTML = msgJson.uploadPercent.toString() + '%&nbsp';
        });
        evtSource.addEventListener("error", (event) => {
            // If an error occurs close the connection, then wait 5 seconds and try again.
            console.log(`SSE error occurred while attempting to connect to ${evtSource.url}`);
            evtSource.close();
            delayStatusFn.push(setTimeout(checkStatus, 5000));
        });
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
    let prerelease = document.getElementById("prerelease").checked;
    const latest = releases
        .sort((a, b) => {
            return Date.parse(b.created_at) - Date.parse(a.created_at);
        })
        .find((obj) => {
            // if prerelease allowed, select first object.  Else select first object that not a prerelease.
            return (prerelease || !obj.prerelease);
        });
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
    clearTimeout(checkHeartbeat);
    const spanDots = document.getElementById("dotdot3");
    document.getElementById("modalTitle").innerHTML = "";
    document.getElementById("updateMsg").innerHTML = msg;
    if (document.getElementById("updateDialog")) {
        document.getElementById("updateDialog").style.display = "none";
        document.getElementById("modalClose").style.display = 'none';
    }
    document.getElementById("updatePercent").style.display = 'none';
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

async function showUpdateDialog() {
    const modalFlashCRC = document.getElementById("modalFlashCRC");
    modalFlashCRC.innerHTML = "Checking Flash CRC...";
    modalFlashCRC.style.color = '';
    document.getElementById("myModal").style.display = 'block';
    const response = await fetch("checkflash", {
        method: "GET",
        cache: "no-cache"
    });
    const result = (await response.text()).trim().toLowerCase();
    if (result === 'true') {
        modalFlashCRC.style.color = 'green';
        modalFlashCRC.innerHTML = "Flash CRC okay.";
    } else {
        modalFlashCRC.style.color = 'red';
        modalFlashCRC.innerHTML = "WARNING: Flash CRC check failed. You must flash new firmware by USB cable to recover, please consult <a href='https://github.com/ratgdo/homekit-ratgdo?tab=readme-ov-file#flash-crc-errors' style='color:red'>documentation.</a> RATGDO device may not restart if you reboot now.";
    }
}

// Handles request to update server firmware from either GitHub (default) or from
// a user provided file.
async function firmwareUpdate(github = true) {
    const inputElem = document.querySelector('input[type="file"]');
    // check that a file name was provided
    if (!github && (inputElem.files.length == 0)) {
        console.log("No file name provided");
        alert("You must select a file to upload.");
        return;
    }
    // check if authenticated, before update
    if (!(await checkAuth() && confirm(`Update firmware from ${github ? 'GitHub' : 'local file'}, are you sure? Do not close browser until complete.`))) {
        return;
    }
    var showRebootMsg = false;
    var rebootMsg = "";
    const spanDots = document.getElementById("dotdot3");
    const aniDots = dotDotDot(spanDots);
    try {
        document.getElementById("updateDialog").style.display = "none";
        document.getElementById("updateMsg").innerHTML = "Do not close browser until update completes. Device will reboot when complete.<br><br>Uploading...";
        document.getElementById("updateDotDot").style.display = "block";
        let bin;
        let binMD5;
        let expectedMD5;
        if (github) {
            if (!serverStatus.latestVersion) {
                console.log("Cannot download firmware, latest version unknown");
                alert("Firmware version at GitHub is unknown, cannot update directly from GitHub.");
                return;
            }
            console.log("Download firmware from: " + serverStatus.downloadURL);
            document.getElementById("updateMsg").innerHTML = "Do not close browser until update completes. Device will reboot when complete.<br><br>Downloading from GitHub...";
            // For GitHub we will check integrity of downloaded file with MD5 hash.
            const regex = /\.bin$/;
            let response = await fetch(serverStatus.downloadURL.replace(regex, ".md5"), {
                method: "GET",
                cache: "no-cache",
                redirect: "follow",
                headers: {
                    "Accept": "text/plain",
                },
            });
            if (response.status != 200) {
                if (confirm("Firmware MD5 checksum file not found on GitHub. Continue update process anyway?")) {
                    console.log("Firmware MD5 checksum file missing, user requested continue anyway.");
                    expectedMD5 = "";
                } else {
                    console.log(`Firmware update canceled as MD5 checksum file missing`);
                    return;
                }
            }
            else {
                expectedMD5 = (await response.text()).trim().toLowerCase();
                console.log(`Expected firmware MD5: ${expectedMD5}`);
            }
            response = await fetch(serverStatus.downloadURL, {
                method: "GET",
                cache: "no-cache",
                redirect: "follow",
                headers: {
                    "Accept": "application/octet-stream",
                },
            });
            bin = await response.arrayBuffer();
            binMD5 = MD5(new Uint8Array(bin));
            if ((expectedMD5 != "") && (expectedMD5 != binMD5)) {
                console.log(`Firmware MD5: ${binMD5}`);
                alert("Received firmware MD5 does not match expected MD5. Firmware update aborted.");
                return;
            }
        } else {
            // For local filesystem we will not require a MD5 checksum file check.
            bin = await inputElem.files[0].arrayBuffer();
            binMD5 = MD5(new Uint8Array(bin));
        }
        console.log(`Firmware upload size: ${bin.byteLength}`);
        console.log(`Firmware MD5: ${binMD5}`);
        // Tell server we are about to upload new firmware and its MD5 hash
        await setGDO("updateUnderway", JSON.stringify({
            md5: binMD5,
            size: bin.byteLength,
            uuid: clientUUID
        }));
        document.getElementById("updateMsg").innerHTML = "Do not close browser until update completes. Device will reboot when complete.<br><br>Uploading...";
        // Set initial percentage to zero
        let spanPercent = document.getElementById("updatePercent");
        spanPercent.style.display = 'initial';
        spanPercent.innerHTML = '00%&nbsp';
        // Upload the file
        const formData = new FormData();
        formData.append("content", new Blob([bin]));
        var response = await fetch(`update?action=update&size=${bin.byteLength}&md5=${binMD5}`, {
            method: "POST",
            body: formData,
        });
        showRebootMsg = true;
        if (response.status !== 200) {
            rebootMsg = await response.text();
            console.error(`Firmware upload error: ${rebootMsg}`);
            if (confirm(`Firmware upload error: ${rebootMsg} Existing firmware not replaced. Proceed to reboot device? NOTE: Reboot is required to re-enable HomeKit services.`)) {
                rebootRATGDO(false);
            }
            else {
                showRebootMsg = false;
                location.href = "/";
            }
            return;
        }
        // Upload and verify suceeded, so reboot...
        rebootRATGDO(false);
        rebootMsg = "Update complete...";
    }
    finally {
        clearInterval(aniDots);
        if (showRebootMsg) {
            // Additional 10 seconds for new firmware copy on first boot.
            countdown(rebootSeconds + 10, rebootMsg + "<br>RATGDO device rebooting...&nbsp;");
        } else {
            document.getElementById("updateDotDot").style.display = "none";
            document.getElementById("updateDialog").style.display = "block";
        }
    }
}

async function rebootRATGDO(dialog = true) {
    if (dialog) {
        /*** Disable this as we don't have any CRC problems anymore.
        document.getElementById("pleaseWait").style.display = "block";
        loaderElem.style.visibility = "visible";
        const response = await fetch("checkflash", {
            method: "GET",
            cache: "no-cache"
        });
        const result = (await response.text()).trim().toLowerCase();
        loaderElem.style.visibility = "hidden";
        // Give browser a moment to actually hide the spinner...
        await new Promise(r => setTimeout(r, 50));
        document.getElementById("pleaseWait").style.display = "none";
        */
        let txt = "Reboot RATGDO, are you sure?";
        /*
        if (result !== 'true') {
            txt = "WARNING: Flash CRC check failed. You must flash new firmware by USB cable to recover, please consult documentation. RATGDO device may not restart if you reboot now. Reboot anyway?";
        }
        */
        if (!confirm(txt)) return;
    }
    var response = await fetch("reboot", {
        method: "POST",
    });
    if (response.status !== 200) {
        console.warn("Error attempting to reboot RATGDO");
        return;
    }
    if (dialog) countdown(rebootSeconds, "RATGDO device rebooting...&nbsp;");
}

async function unpairRATGDO() {
    // check if authenticated, before update
    if (!(await checkAuth() && confirm('Pair to new HomeKit, are you sure?'))) {
        return false;
    }
    loaderElem.style.visibility = "visible";
    var response = await fetch("reset", {
        method: "POST",
    });
    loaderElem.style.visibility = "hidden";
    if (response.status !== 200) {
        console.warn("Error attempting to unpair and reboot RATGDO");
        return;
    }
    countdown(rebootSeconds, "RATGO un-pairing and rebooting...&nbsp;");
}

async function checkAuth(loader = true) {
    auth = false;
    if (loader) loaderElem.style.visibility = "visible";
    var response = await fetch("auth", {
        method: "GET",
    });
    if (loader) loaderElem.style.visibility = "hidden";
    // Give browser a moment to actually hide the spinner...
    await new Promise(r => setTimeout(r, 50));
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
        loaderElem.style.visibility = "visible";
        if (!await checkAuth(false)) {
            return false;
        }
        const formData = new FormData();
        for (let i = 0; i < args.length; i = i + 2) {
            // Only transmit setting if value has changed
            if (serverStatus[args[i]] != args[i + 1]) {
                console.log(`Set: ${args[i]} to: ${args[i + 1]}`);
                formData.append(args[i], args[i + 1]);
            }
        }
        if (Array.from(formData.keys()).length > 0) {
            var response = await fetch("setgdo", {
                method: "POST",
                body: formData,
                signal: AbortSignal.timeout(2000),
            });
            if (response.status !== 200) {
                console.warn("Error setting RATGDO state");
                return true;;
            }
            else {
                const result = await response.text();
                if (result.includes('Reboot')) {
                    console.log('Server settings saved, reboot required');
                    return true;
                }
                return false;
            }
        }
        else {
            console.log('setGDO: No values changed');
            return false;
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
    finally {
        loaderElem.style.visibility = "hidden";
    }
    return false;
}

async function changePassword() {
    // newPW defined in index.html
    if (newPW.value === "") {
        alert("New password cannot be blank");
        return;
    }
    if (newPW.value !== confirmPW.value) {
        alert("Passwords do not match");
        return;
    }
    let www_username = document.getElementById("newUserName").value.substring(0, 30);
    if (www_username.length == 0) www_username = serverStatus.userName ?? "admin";
    const www_realm = "RATGDO Login Required";
    // MD5() function expects a Uint8Array typed ArrayBuffer...
    const passwordHash = MD5((new TextEncoder).encode(www_username + ":" + www_realm + ":" + newPW.value));
    console.log("Set new credentials to: " + passwordHash);
    // await setGDO("credentials", passwordHash);
    await setGDO("credentials", JSON.stringify({
        username: www_username,
        credentials: passwordHash
    }));
    clearTimeout(checkHeartbeat);
    // On success, go to home page.
    // User will have to re-authenticate to get back to settings.
    location.href = "/";
    return;
}

function getMotionTriggers() {
    let bitset = 0;
    bitset += (document.getElementById("motionMotion").checked) ? 1 : 0;
    bitset += (document.getElementById("motionObstruction").checked) ? 2 : 0;
    //bitset += (document.getElementById("motionLight").checked) ? 4 : 0;
    //bitset += (document.getElementById("motionDoor").checked) ? 8 : 0;
    //bitset += (document.getElementById("motionLock").checked) ? 16 : 0;
    bitset += (document.getElementById("motionWallPanel").checked) ? 28 : 0;
    return bitset;
}

function setMotionTriggers(bitset) {
    document.getElementById("motionMotion").checked = (bitset & 1) ? true : false;
    document.getElementById("motionObstruction").checked = (bitset & 2) ? true : false;
    //document.getElementById("motionLight").checked = (bitset & 4) ? true : false;
    //document.getElementById("motionDoor").checked = (bitset & 8) ? true : false;
    //document.getElementById("motionLock").checked = (bitset & 16) ? true : false;
    document.getElementById("motionWallPanel").checked = (bitset & 28) ? true : false;
};

async function saveSettings() {
    if (!confirm('Save Settings. Reboot may be required, are you sure?')) {
        return;
    }
    const gdoSec = (document.getElementById("gdosec1").checked) ? '1' : '2';
    const pwReq = (document.getElementById("pwreq").checked) ? '1' : '0';
    const motionTriggers = getMotionTriggers();
    const LEDidle = (document.getElementById("LEDidle").checked) ? 1 : 0;
    let rebootHours = Math.max(Math.min(parseInt(document.getElementById("rebootHours").value), 72), 0);
    if (isNaN(rebootHours)) rebootHours = 0;
    let newDeviceName = document.getElementById("newDeviceName").value.substring(0, 30);
    if (newDeviceName.length == 0) newDeviceName = serverStatus.deviceName;
    const wifiPhyMode = (document.getElementById("wifiPhyMode3").checked) ? '3'
        : (document.getElementById("wifiPhyMode2").checked) ? '2'
            : (document.getElementById("wifiPhyMode1").checked) ? '1'
                : '0';
    const wifiPower = Math.max(Math.min(parseInt(document.getElementById("wifiPower").value), 20), 0);
    let TTCseconds = Math.max(Math.min(parseInt(document.getElementById("TTCseconds").value), 60), 0);
    if (isNaN(TTCseconds)) TTCseconds = 0;

    const syslogEn = (document.getElementById("syslogEn").checked) ? '1' : '0';
    let syslogIP = document.getElementById("syslogIP").value.substring(0, 15);
    if (syslogIP.length == 0) syslogIP = serverStatus.syslogIP;

    const staticIP = (document.getElementById("staticIP").checked) ? '1' : '0';
    let localIP = document.getElementById("IPaddress").value.substring(0, 15);
    if (localIP.length == 0) localIP = serverStatus.localIP;
    let subnetMask = document.getElementById("IPnetmask").value.substring(0, 15);
    if (subnetMask.length == 0) subnetMask = serverStatus.subnetMask;
    let gatewayIP = document.getElementById("IPgateway").value.substring(0, 15);
    if (gatewayIP.length == 0) gatewayIP = serverStatus.gatewayIP;
    let nameserverIP = document.getElementById("IPnameserver").value.substring(0, 15);
    if (nameserverIP.length == 0) nameserverIP = serverStatus.nameserverIP;
    const enableNTP = (document.getElementById("enableNTP").checked) ? '1' : '0';


    // check IP addresses valid
    const regexIPv4 = /^(([0-9]|[1-9][0-9]|1[0-9]{2}|2[0-4][0-9]|25[0-5])\.){3}([0-9]|[1-9][0-9]|1[0-9]{2}|2[0-4][0-9]|25[0-5])$/i;
    if (!(regexIPv4.test(localIP) && regexIPv4.test(subnetMask) && regexIPv4.test(gatewayIP) && regexIPv4.test(nameserverIP) && regexIPv4.test(syslogIP))) {
        console.error(`Invalid IP address(s): ${localIP} / ${subnetMask} / ${gatewayIP} / ${nameserverIP} / ${syslogIP}`);
        alert(`Invalid IP address(s): ${localIP} / ${subnetMask} / ${gatewayIP} / ${nameserverIP} / ${syslogIP}`);
        return;
    }

    const reboot = await setGDO("GDOSecurityType", gdoSec,
        "passwordRequired", pwReq,
        "rebootSeconds", (rebootHours * 60 * 60),
        "deviceName", newDeviceName,
        "wifiPhyMode", wifiPhyMode,
        "wifiPower", wifiPower,
        "TTCseconds", TTCseconds,
        "motionTriggers", motionTriggers,
        "LEDidle", LEDidle,
        "staticIP", staticIP,
        "localIP", localIP,
        "subnetMask", subnetMask,
        "gatewayIP", gatewayIP,
        "nameserverIP", nameserverIP,
        "enableNTP", enableNTP,
        "syslogEn", syslogEn,
        "syslogIP", syslogIP
    );
    if (reboot) {
        countdown(rebootSeconds, "Settings saved, RATGDO device rebooting...&nbsp;");
    }
    else {
        // No need to reboot, but return to main page to reload status.
        location.href = "/";
    }
    return;
}

async function resetDoor() {
    if (confirm('Reset door rolling codes and presence of motion sensor. Settings will not change but device will reboot, are you sure?')) {
        await setGDO("resetDoor", true);
        countdown(rebootSeconds, "Door reset, RATGDO device rebooting...&nbsp;");
    }
    return;
}

async function setSSID() {
    if (confirm('This will reboot RATGDO device into Soft Access Point mode from where you can '
        + 'select a WiFi network SSID.\n\nYou must connect your laptop or mobile device to '
        + 'WiFi Network: "' + document.getElementById("deviceName").innerHTML.replace(/\s/g, '-') + '" and then connect your browser to IP address: '
        + '192.168.4.1\n\nAre you sure?')) {
        await setGDO("softAPmode", true);
        countdown(rebootSeconds, "RATGDO device rebooting...&nbsp;");
    }
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
