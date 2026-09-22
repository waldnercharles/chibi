#!/usr/bin/env node
import {readFileSync} from 'node:fs';
import {editorCommand} from './editor-command.mjs';
try {
  process.stdout.write(JSON.stringify(await editorCommand(JSON.parse(readFileSync(process.argv[2] || 0,'utf8'))),null,2)+'\n');
} catch(error) {process.stderr.write(error.message+'\n');process.exitCode=1;}
