const assert = require('node:assert/strict');
const { test } = require('node:test');
const { readFileSync } = require('node:fs');
const vm = require('node:vm');

// installed is the firmware version as the page holds it, with the leading "v" already added.
async function check(installed, latest) {
    const elements = new Map();
    const element = id => {
        if (!elements.has(id)) elements.set(id, { innerHTML: '', checked: false });
        return elements.get(id);
    };
    const context = vm.createContext({
        console: { log() {}, warn() {}, error() {} },
        crypto: require('node:crypto').webcrypto, Uint8Array,
        document: { getElementById: element },
        setInterval() {}, clearInterval() {},
        fetch: async () => ({ status: 200, json: async () => [{ tag_name: latest, prerelease: false,
            created_at: '2026-01-01T00:00:00Z', assets: [{ name: `homekit-ratgdo32-${latest}.firmware.bin`,
                content_type: 'application/octet-stream' }] }] }),
    });
    vm.runInContext(readFileSync('src/www/functions.js', 'utf8'), context);
    context.serverStatus.firmwareVersion = installed;
    await context.checkVersion();
    return element('newversion').innerHTML;
}

const newest = 'You have newest release';
const update = tag => `Update available  (${tag})`;

test('same version is the newest release', async () => {
    assert.equal(await check('v3.5.6', 'v3.5.6'), newest);
});

test('older and newer versions', async () => {
    assert.equal(await check('v3.5.5', 'v3.5.6'), update('v3.5.6'));
    assert.equal(await check('v3.5.6', 'v3.5.5'), newest);
    assert.equal(await check('v3.4.6', 'v3.5.0'), update('v3.5.0'));
});

test('multi-digit components compare as numbers', async () => {
    assert.equal(await check('v3.5.9', 'v3.5.10'), update('v3.5.10'));
    assert.equal(await check('v3.5.10', 'v3.5.9'), newest);
    assert.equal(await check('v3.1.9', 'v3.1.10'), update('v3.1.10'));
    assert.equal(await check('v0.9.0', 'v0.12.0'), update('v0.12.0'));
    assert.equal(await check('v3.5.9-Dev', 'v3.5.10'), update('v3.5.10'));
});

test('a multi-digit number right after the leading v compares as a number', async () => {
    assert.equal(await check('v9.0.0', 'v10.0.0'), update('v10.0.0'));
    assert.equal(await check('v10.0.0', 'v9.9.9'), newest);
});

test('a beta or VERSION_TAG suffix sorts after the same release, as before', async () => {
    assert.equal(await check('v3.5.4beta', 'v3.5.4'), newest);
    assert.equal(await check('v3.5.7-Dev', 'v3.5.7'), newest);
    assert.equal(await check('v3.5.4beta', 'v3.5.5'), update('v3.5.5'));
});
