#include <stdio.h>
#include <math.h>
#include <ugens.h>

#define TLEN 20  /* maximum number of time/tempo pairs */
#define TLENP 21
#define TWO  2.

static float xtime[TLENP],temp[TLENP],rxtime[TLENP],accel[TLENP],BASIS = 60.;
static short tset = 0,npts;

float time_beat(float timein);
float beat_time(float beatin);


double
tbase(double p[], int n_args)
{
	BASIS = p[0];
	return 0.0;
}


double
tempo(double p[], int n_args)
{
	short m;
	float dur,prvbt;
	if(!n_args) {
		rtcmix_advise("tempo", "Tempo changes cleared out");
		tset = 0;
		return -1.0;
	}
    for(m=0;m<TLEN;m++) { xtime[m]=temp[m]=rxtime[m]=accel[m]=0; }
	for(m=0,npts=1; m<TLEN; m += 2, npts++) {
		if((m) && (m >= n_args)) break;
		xtime[npts] = p[m];
		temp[npts] = p[m+1]/BASIS;
        if (temp[npts] == 0.0) { return die("tempo", "tempo value cannot be zero - did you reverse your arguments?"); }
    }
    tset = 1;
	rxtime[npts] = xtime[npts] = .999999e+10;
	temp[npts] = temp[npts-1];
	temp[0] = temp[1];
	xtime[0] = rxtime[0] = accel[0] = prvbt = 0.;
	for(m=0; m<npts; m++) {
		dur = xtime[m+1] - xtime[m];
		if (dur == 0.0f) {
			accel[m] = 0.0f;
			rxtime[m+1] = rxtime[m];
        }
		else 	{
			accel[m] = (float)(pow((double)temp[m+1],TWO) - pow((double)temp[m],TWO))/(2.0f*dur);
            if (accel[m] == 0.0f) {
                rxtime[m+1] = dur/temp[m]+prvbt;
            }
            else {
                rxtime[m+1] = (float) (sqrt(pow((double)temp[m],TWO)+
					(double)(TWO* accel[m]*dur))-temp[m])/
					accel[m] + prvbt;
            }
		}
		prvbt = rxtime[m+1];
	}
/*
for(m=0; m<=npts; m++) printf("%d %f %f %f %f\n",m,temp[m],accel[m],rxtime[m],xtime[m]);
*/
	return 0.0;
}


float
time_beat(float timein)
{
	int m = 0;
	float durp = 0;

	if (timein < 0.0f)
		timein = 0.0f;

	if(!tset) return(timein);

    if (timein > 0.0f) {
        for(m=0; m<=npts; m++) {
            if(timein > xtime[m] && timein <= xtime[m+1]) {
                durp = timein-xtime[m];
                break;
            }
        }
    }
    if (accel[m] == 0.0f) {
        return(durp/temp[m] + rxtime[m]);
    }
	return(((float)sqrt(pow((double)temp[m],TWO)+ 
		(double)(TWO * accel[m] * durp)) - temp[m])/
		accel[m] + rxtime[m]);
}


float
beat_time(float beatin)    /* returns beats from times */
{
	int m=0;
	if(!tset) return(beatin);

    if (beatin != 0.0f) {
        for(m=0; m<=npts; m++) {
            if (beatin > rxtime[m] && beatin <= rxtime[m+1]) {
                break;
            }
        }
    }
    if (accel[m] == 0.0f) {
        return((beatin-rxtime[m])*temp[m] + xtime[m]);
    }
	return(((float)pow((double)((beatin-rxtime[m]) * accel[m]+temp[m]),TWO)
		-(float)pow((double)temp[m],TWO))/(TWO * accel[m]) + xtime[m]);
}


double
t(double p[], int n_args)
{
	float x,y;
	x = time_beat(p[0]);
	y = beat_time(p[0]);
	printf("%f %f %f\n",p[0],x,y);
	return 0.0;
}

