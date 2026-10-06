#include <cstdarg>
void logmsg(const char*,...){}
/* verifica diretta della funzione C input_joy1dat */
#include <cstdio>
#include <cstdint>
extern uint16_t input_joy1dat(void);
extern void input_set_joy(int,int,int,int,int);
int main(){
    struct { const char*n; int u,d,l,r; uint16_t want; } t[] = {
        {"fermo",0,0,0,0,0x0000},{"su",1,0,0,0,0x0100},{"giu",0,1,0,0,0x0001},
        {"sx",0,0,1,0,0x0300},{"dx",0,0,0,1,0x0003},{"su+dx",1,0,0,1,0x0103},
    };
    int fail=0;
    for(auto&x:t){ input_set_joy(x.u,x.d,x.l,x.r,0); uint16_t v=input_joy1dat();
        bool ok=v==x.want; printf("%s %s: %04X (atteso %04X)\n",ok?"OK ":"FAIL",x.n,v,x.want);
        if(!ok)fail++; }
    return fail?1:0;
}
