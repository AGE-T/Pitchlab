c=[0.25,0.346058071,0.24391453,0.119830653,0.0348676369]
def P(f): return c[0]+c[1]*f+c[2]*f*f+c[3]*f**3+c[4]*f**4
print("sum",sum(c))
for f in (0,0.25,0.5,0.75,1.0,1.004,1.0047,1.25,1.5,2.0):
    print("f=%6.4f P=%.9f  1/(64P)=%.9f"%(f,P(f),1/(64*P(f))))
# solve P(f)=1
import bisect
lo,hi=1.0,1.1
for _ in range(80):
    m=(lo+hi)/2
    if P(m)<1: lo=m
    else: hi=m
print("P(f)=1 at f=%.9f"%lo)
# if R=1/64 ratio: step=P(f)*ratio
for f in (0.5,1.0,lo):
    print("f=%.6f step(at R=1/64)=%.9f cents=%.3f"%(f,P(f),1200*__import__("math").log2(P(f))))
