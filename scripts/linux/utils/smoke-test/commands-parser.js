'use strict';

const { Trim } = require('./util');

function SplitCommandSpecSegments(inputLine) {
    const segments = [];
    let inQuotes = 0;
    let segment = '';
    const length = inputLine.length;

    for (let index = 0; index < length; index++) {
        const character = inputLine[index];

        if (character === '"') {
            inQuotes = inQuotes === 0 ? 1 : 0;
            segment += character;
            continue;
        }

        if (character === '|' && inQuotes === 0) {
            segments.push(segment);
            segment = '';
            continue;
        }

        segment += character;
    }

    segments.push(segment);
    return segments;
}

function ParseCommandSpec(line) {
    const spec = {
        actionType: '',
        actionText: '',
        expectedText: '',
        compareSource: '',
        compareDownloaded: '',
        preActionDelaySeconds: '',
        timeoutSeconds: '',
    };

    const segments = SplitCommandSpecSegments(line);
    for (const rawSegment of segments) {
        const part = Trim(rawSegment);

        let match = /^command:[ \t]*"([^"]*)"$/.exec(part);
        if (match) {
            spec.actionType = 'command';
            spec.actionText = match[1];
            continue;
        }

        match = /^hotkey:[ \t]*"([^"]*)"$/.exec(part);
        if (match) {
            spec.actionType = 'hotkey';
            spec.actionText = match[1];
            continue;
        }

        match = /^type:[ \t]*"([^"]*)"$/.exec(part);
        if (match) {
            spec.actionType = 'type';
            spec.actionText = match[1];
            continue;
        }

        match = /^log:[ \t]*"([^"]*)"$/.exec(part);
        if (match) {
            spec.expectedText = match[1];
            continue;
        }

        match = /^file-size-compare:[ \t]*"([^"]*)"[ \t]+"([^"]*)"$/.exec(part);
        if (match) {
            spec.compareSource = match[1];
            spec.compareDownloaded = match[2];
            continue;
        }

        match = /^before:[ \t]*([0-9]+(\.[0-9]+)?)$/.exec(part);
        if (match) {
            spec.preActionDelaySeconds = match[1];
            continue;
        }

        match = /^timeout:[ \t]*([0-9]+)$/.exec(part);
        if (match) {
            spec.timeoutSeconds = match[1];
            continue;
        }

        throw new Error(`Invalid command spec segment: ${part}`);
    }

    if (!spec.actionType || !spec.actionText) {
        throw new Error(`Invalid command spec, missing command or hotkey: ${line}`);
    }

    return spec;
}

function ExpandCommandsContent(content, baseUrl) {
    if (!baseUrl) {
        return content;
    }
    return content.split('@LOCAL_HTTP_BASE_URL@').join(baseUrl);
}

module.exports = {
    SplitCommandSpecSegments,
    ParseCommandSpec,
    ExpandCommandsContent,
};
