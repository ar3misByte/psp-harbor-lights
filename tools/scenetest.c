#include "scene.h"
#include "gfx.h"
#include "pal.h"
#include <stdio.h>
static uint32_t lo[BUF_W*SCENE_H], fb[BUF_W*272];
static void save(const char*p){FILE*f=fopen(p,"wb");fprintf(f,"P6\n480 272\n255\n");for(int y=0;y<272;y++)for(int x=0;x<480;x++){uint32_t c=fb[y*BUF_W+x];unsigned char q[3]={C_R(c),C_G(c),C_B(c)};fwrite(q,1,3,f);}fclose(f);}
int main(){scene_init(); SceneState s={0}; s.n=10; s.sel=1; s.level=10;
 for(int i=0;i<10;i++){s.m[i].color=i;s.m[i].style=i%4;s.m[i].power=(i%4)/3.0f;}
 s.m[3].alert=1; s.m[5].complete=1; s.m[1].power=1;
 int hours[4]={11,18,22,6};
 for(int k=0;k<4;k++){ s.hour=hours[k]; s.minute=0; s.t=3.0f+k; scene_launch(5,P_GOLD); for(int j=0;j<60;j++) scene_update(0.05f);
  scene_draw(lo,&s); gfx_set_target(fb); scene_blit2x(fb,lo); char n[64]; sprintf(n,"/tmp/claude-0/coast%d.ppm",k); save(n);}
 return 0;}
