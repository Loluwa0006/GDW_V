
                                                                    
**Temiloluwa Awosiyan

100977745 **

**Rima**

Atmospheric walking simulator game.

"What did you notice was impacting the performance of your project?"

There was a damage function being called when a VFX was created every frame that was causing performance issues due to the complexity of the formula.

"Which pattern(s) did you implement, and what behavior did they replace?"

I implemented a dirty flag system to replace the old system of performing a damage function per VFX even if the VFX wasn't enabled.

I implemented an object pool system to reuse the same VFX repeatedly instead of creating and destroying a VFX every time I required it.

Pre Dirty Flag:
https://youtu.be/ZftJT_wLzrU

<img width="2447" height="1450" alt="Screenshot 2026-11-16 224956" src="https://github.com/user-attachments/assets/95c02ab7-205f-4ac4-a8c6-ea4e37ed01ad" />

Post Dirty Flag:
https://youtu.be/zlCo6jzIpCU 

<img width="2427" height="1446" alt="Screenshot 2026-11-16 223114" src="https://github.com/user-attachments/assets/76a49563-3e8c-45d9-b803-42a183a1fc4c" />


"Do your patterns / technique implementations improve performance? Why or why not?"

Yes, my pattern improved performance, because it restricted the amount of times the damage function had to be called. Instead of every VFX in the pool, it was only the VFX in the pool that were active.

"Can you demonstrate what the profiling process revealed?"

Profiling revealed that the performance of the game immediately tanks on startup when using the dirty flag system due to the large amount of VFX created in a single call, but gradually becomes better before reaching an average that is about 50% higher frames then the non dirty flag system.

Blueprint Screenshots:

VFX Manager Event Tick (Dirty Flag Implementation)

I create a large amount of VFX per frame, and call the damage function per active VFX.
<img width="1760" height="996" alt="Screenshot 2026-11-16 230053" src="https://github.com/user-attachments/assets/92993643-4c42-4ccb-bc66-04c81f653ee3" />

VFX Manager Event Tick (No Dirty Flag Implementation)
Loops over all VFX in the pool instead of just the active ones.
<img width="1760" height="996" alt="Screenshot 2026-11-16 230053" src="https://github.com/user-attachments/assets/83e1fc05-d47c-4ff7-8c33-097b578793f2" />

OnDamage()
<img width="1811" height="1227" alt="Screenshot 2026-11-16 230312" src="https://github.com/user-attachments/assets/ab668fea-ad14-4a43-840f-c74ba3dc137c" />

VFX Pool Creation (Part of Begin Play)
<img width="2191" height="1337" alt="Screenshot 2026-11-16 230419" src="https://github.com/user-attachments/assets/8dc9f539-debd-4b03-9569-c9548316a028" />

GetNewInteractVFXFromPool()
<img width="1817" height="1216" alt="Screenshot 2026-11-16 230455" src="https://github.com/user-attachments/assets/439e0b6d-2503-499e-b885-17486d7bcc97" />

PlayVFXAttached() (VFX Creation System Before Object Pooling Implementation)
<img width="1820" height="1217" alt="Screenshot 2026-11-16 230727" src="https://github.com/user-attachments/assets/5c73b71d-f4dc-4d4b-96b0-9064fdbc6acc" />

