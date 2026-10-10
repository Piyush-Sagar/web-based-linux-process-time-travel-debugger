import {defineConfig} from '@playwright/test';
import process from 'node:process';
export default defineConfig({
  testDir:'./e2e', fullyParallel:false, workers:1, timeout:60000,
  use:{baseURL:'http://127.0.0.1:8092',headless:true,viewport:{width:1440,height:1100},trace:'retain-on-failure'},
  webServer:{command:process.platform==='win32'?'wsl -d Ubuntu -- bash ../scripts/e2e-server.sh':'bash ../scripts/e2e-server.sh',url:'http://127.0.0.1:8092/api/health',timeout:30000,reuseExistingServer:false},
  reporter:[['list'],['json',{outputFile:'../evidence/browser-tests.json'}]],
});
