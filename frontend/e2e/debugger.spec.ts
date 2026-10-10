import {expect,test} from '@playwright/test';
import type {Page} from '@playwright/test';

async function reset(page:Page) {
  const list=await (await page.request.get('/api/sessions')).json();
  for(const s of list)expect((await page.request.delete(`/api/sessions/${s.id}`)).ok()).toBeTruthy();
}
async function recordCrash(page:Page) {
  await page.goto('/');await expect(page.getByRole('button',{name:'Launch process'})).toBeEnabled();
  await page.getByRole('button',{name:'Launch process'}).click();
  await expect(page.getByTestId('view-mode')).toHaveText('Live recording state');
  await page.getByRole('button',{name:'Continue',exact:true}).click();
  await expect(page.locator('.status-pill')).toHaveText('crashed',{timeout:15000});
  await expect(page.locator('.fault-banner')).toContainText('SIGSEGV');
  const sessions=await (await page.request.get('/api/sessions')).json();
  const s=sessions.find((x:{demo:string})=>x.demo==='crash');
  const events=await (await page.request.get(`/api/sessions/${s.id}/events`)).json();return {s,events};
}
test('crash → inspect history → restore terminated process → replay multiple chosen steps',async({page})=>{
  const errors:string[]=[];page.on('pageerror',e=>errors.push(e.message));
  await reset(page);const {s,events}=await recordCrash(page);
  await expect(page.locator('.output pre')).toHaveText('recorded once\n');
  await page.getByRole('button',{name:'Deliver signal'}).click();
  await expect(page.locator('.status-pill')).toHaveText('terminated');
  await page.getByLabel('Step',{exact:true}).fill('39');
  await expect(page.getByTestId('view-mode')).toHaveText('Historical inspection');
  await expect(page.locator('.view-banner')).toContainText('Execution has not been restored');
  for(const target of [39,1,57,140,141]) {
    await page.getByLabel('Step',{exact:true}).fill(String(target));
    await expect(page.getByRole('button',{name:'Restore & replay to step'})).toBeEnabled();
    await page.getByRole('button',{name:'Restore & replay to step'}).click();
    await expect(page.getByTestId('view-mode')).toHaveText('Live restored execution');
    await expect(page.getByTestId('replay-proof')).toContainText(`step ${target}`);
    for(const [name,value] of Object.entries(events[target].registers))await expect(page.locator(`[data-register="${name}"]`)).toHaveText(value as string);
    for(let i=0;i<8;i++)await expect(page.locator(`[data-memory="${i}"]`)).toHaveText(events[target].memory.hex.slice(i*16,i*16+16).match(/../g).join(' '));
    const live=await (await page.request.get(`/api/sessions/${s.id}`)).json();expect(live.pid).toBeGreaterThan(0);expect(live.pid).not.toBe(s.pid);expect(live.observed_output_hex).toBe('');
  }
  await page.getByRole('combobox',{name:'Event filter'}).selectOption('events');
  await expect(page.locator('.event-list')).toContainText('getrandom');await expect(page.locator('.event-list')).toContainText('clock_gettime');
  await page.screenshot({path:'../evidence/demo.png',fullPage:true});
  expect(errors).toEqual([]);
});
test('continue can be paused; unsupported syscalls report an explicit failure',async({page})=>{
  await reset(page);await page.goto('/');
  await page.getByLabel('Demo executable').selectOption('loop');await page.getByRole('button',{name:'Launch process'}).click();
  await expect(page.getByTestId('view-mode')).toBeVisible();await page.getByRole('button',{name:'Continue',exact:true}).click();
  await expect(page.getByRole('button',{name:'Pause',exact:true})).toBeEnabled();await page.getByRole('button',{name:'Pause',exact:true}).click();
  await expect(page.locator('.status-pill')).toHaveText('paused');
  const initial=await (await page.request.get('/api/sessions')).json();await page.waitForTimeout(150);
  const after=await (await page.request.get('/api/sessions')).json();expect(after[0].cursor).toBe(initial[0].cursor);
  await page.getByLabel('Demo executable').selectOption('forbidden');await page.getByRole('button',{name:'Launch process'}).click();
  await expect(page.locator('.workspace-heading h2')).toHaveText('forbidden.cpp');await page.getByRole('button',{name:'Continue',exact:true}).click();
  await expect(page.locator('.status-pill')).toHaveText('rejected');
  // Background rejection is attached to the real session, even without a failed HTTP response.
  await expect(page.locator('[role="alert"]')).toContainText('unsupported syscall 39');
});
test('narrow layout, keyboard history input, and reduced motion use real recorded states',async({page})=>{
  await reset(page);await page.setViewportSize({width:375,height:812});await page.emulateMedia({reducedMotion:'reduce'});
  await recordCrash(page);await page.getByLabel('Step',{exact:true}).fill('19');
  await expect(page.getByTestId('view-mode')).toHaveText('Historical inspection');
  await page.getByLabel('Historical step',{exact:true}).focus();await page.keyboard.press('ArrowRight');
  await expect(page.getByLabel('Step',{exact:true})).toHaveValue('20');
  expect(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth)).toBeTruthy();
  await page.getByRole('button',{name:'Restore & replay to step'}).click();await expect(page.getByTestId('view-mode')).toHaveText('Live restored execution');
  await page.screenshot({path:'../evidence/mobile.png',fullPage:true});
  await page.setViewportSize({width:812,height:375});expect(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth)).toBeTruthy();
});
