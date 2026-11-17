
                                                                    
Temiloluwa Awosiyan

100977745 

**Rima**

Atmospheric walking simulator game.

"What did you notice was impacting the performance of your project?"

There was a damage function being called when a VFX was created every frame that was causing performance issues due to the complexity of the formula.

"Which pattern(s) did you implement, and what behavior did they replace?"

I implemented a dirty flag system to replace the old system of performing a damage function per VFX even if the VFX wasn't enabled.

I implemented an object pool system to reuse the same VFX repeatedly instead of creating and destroying a VFX every time I required it.

Pre Dirty Flag:
https://youtu.be/ZftJT_wLzrU

Post Dirty Flag:
https://youtu.be/zlCo6jzIpCU 



"Do your patterns / technique implementations improve performance? Why or why not?"

Yes, my pattern improved performance, because it restricted the amount of times the damage function had to be called. Instead of every VFX in the pool, it was only the VFX in the pool that were active.

"Can you demonstrate what the profiling process revealed?"

Profiling revealed that the performance of the game was going down over time as the number of active VFX increased.

