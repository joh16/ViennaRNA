/* Last changed Time-stamp: <2000-08-14 21:14:35 ivo> */
/*                
			 minimum free energy
		  RNA secondary structure prediction

			    c Ivo Hofacker
		      original implementation by
			    Walter Fontana
		  
			  Vienna RNA package
*/

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <ctype.h>
#include <string.h>
#include "utils.h"
#include "energy_par.h"
#include "fold_vars.h"
#include "pair_mat.h"
#ifdef __GNUC__
#define INLINE inline PRIVATE
#define UNUSED __attribute__ ((unused))
#else
#define INLINE PRIVATE
#define UNUSED
#endif
/*@unused@*/
static char rcsid[] UNUSED = "$Id: fold.c,v 1.15 2000/08/14 19:17:09 ivo Exp $";

#define PAREN

#define PUBLIC
#ifdef SUBOPT
#define PRIVATE
#else
#define PRIVATE static
#endif

#define STACK_BULGE1  1   /* stacking energies for bulges of size 1 */
#define NEW_NINIO     1   /* new asymetry penalty */

PUBLIC float  fold(char *string, char *structure);
PUBLIC float  energy_of_struct(char *string, char *structure);
PUBLIC int    energy_of_struct_pt(char *string, short *ptable,
				  short *s, short *s1);
PUBLIC void   free_arrays(void);
PUBLIC void   initialize_fold(int length);
PUBLIC void   update_fold_params(void);

PUBLIC int    logML=0;    /* if nonzero use logarithmic ML energy in
			     energy_of_struct */
/*@unused@*/
PRIVATE void  letter_structure(char *structure, int length) UNUSED;
PRIVATE void  parenthesis_structure(char *structure, int length);
PRIVATE void  get_arrays(unsigned int size);
PRIVATE void  scale_parameters(void);
PRIVATE int   stack_energy(int i, char *string);
PRIVATE int   ML_Energy(int i);

PRIVATE void  BP_calculate(char *structure, int *BP, int length);
PRIVATE void  encode_seq(char *sequence);
/*@unused@*/
INLINE  int   oldLoopEnergy(int i, int j, int p, int q, int type, int type_2);
INLINE  int   LoopEnergy(int n1, int n2, int type, int type_2,
			 int si1, int sj1, int sp1, int sq1);
INLINE  int   HairpinE(int i, int j, int type, const char *string);

#define MAXSECTORS      500     /* dimension for a backtrack array */
#define LOCALITY        0.      /* locality parameter for base-pairs */

#define MIN2(A, B)      ((A) < (B) ? (A) : (B))

PRIVATE int stack[NBPAIRS+1][NBPAIRS+1];
PRIVATE int hairpin[31];
PRIVATE int bulge[MAXLOOP+1];
PRIVATE int internal_loop[MAXLOOP+1];
PRIVATE int mismatchI[NBPAIRS+1][5][5];
PRIVATE int mismatchH[NBPAIRS+1][5][5];
PRIVATE int mismatchM[NBPAIRS+1][5][5];
PRIVATE int dangle5[NBPAIRS+1][5];
PRIVATE int dangle3[NBPAIRS+1][5];
PRIVATE int int11[NBPAIRS+1][NBPAIRS+1][5][5];
PRIVATE int int21[NBPAIRS+1][NBPAIRS+1][5][5][5];
PRIVATE int int22[NBPAIRS+1][NBPAIRS+1][5][5][5][5];
PRIVATE int F_ninio[5];
PRIVATE double lxc;
PRIVATE int MLbase;
PRIVATE int MLintern[NBPAIRS+1];
PRIVATE int MLclosing;
PRIVATE int TETRA_ENERGY[40];
PRIVATE int Triloop_E[40];

PRIVATE int *indx; /* index for moving in the triangle matrices c[] and fMl[]*/

PRIVATE int   *c;       /* energy array, given that i-j pair */
PRIVATE int   *cc;      /* linear array for calculating canonical structures */
PRIVATE int   *cc1;     /*   "     "        */
PRIVATE int   *f5;      /* energy of 5' end */
PRIVATE int   *f3;      /* energy of 3' end */
PRIVATE int   *fML;     /* multi-loop auxiliary energy array */
#ifdef SUBOPT
PRIVATE int   *fM1;     /* another multi-loop for subopt */
#endif
PRIVATE int   *Fmi;     /* holds row i of fML (avoids jumps in memory) */
PRIVATE int   *DMLi;    /* DMLi[j] holds MIN(fML[i,k]+fML[k+1,j])  */
PRIVATE int   *DMLi1;   /*             MIN(fML[i+1,k]+fML[k+1,j])  */
PRIVATE int   *DMLi2;   /*             MIN(fML[i+2,k]+fML[k+1,j])  */
PRIVATE short  *S, *S1;
PRIVATE short  *pair_table;
PRIVATE int   init_length=-1;

PRIVATE int   eos_debug=1;
PRIVATE char  alpha[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

/*--------------------------------------------------------------------------*/

void initialize_fold(int length)
{
  unsigned int n;
  if (length<1) nrerror("initialize_fold: argument must be greater 0");
  if (init_length>0) free_arrays();
  get_arrays((unsigned) length);
  scale_parameters();
  make_pair_matrix();
  init_length=length;
  
  for (n = 1; n <= (unsigned) length; n++)
    indx[n] = (n*(n-1)) >> 1;        /* n(n-1)/2 */
}
    
/*--------------------------------------------------------------------------*/

PRIVATE void get_arrays(unsigned int size)
{
  indx = (int *) space(sizeof(int)*(size+1));
  c     = (int *) space(sizeof(int)*((size*(size+1))/2+2));
  fML   = (int *) space(sizeof(int)*((size*(size+1))/2+2));
#ifdef SUBOPT
  fM1    = (int *) space(sizeof(int)*((size*(size+1))/2+2));
#endif   
  f5    = (int *) space(sizeof(int)*(size+2));
  f3    = (int *) space(sizeof(int)*(size+2));
  cc    = (int *) space(sizeof(int)*(size+2));
  cc1   = (int *) space(sizeof(int)*(size+2));
  Fmi   = (int *) space(sizeof(int)*(size+1));
  DMLi  = (int *) space(sizeof(int)*(size+1));
  DMLi1  = (int *) space(sizeof(int)*(size+1));
  DMLi2  = (int *) space(sizeof(int)*(size+1));
  base_pair = (struct bond *) space(sizeof(struct bond)*(1+size/2));
}

/*--------------------------------------------------------------------------*/

void free_arrays(void)
{
  free(indx); free(c); free(fML); free(f5); free(f3); free(cc); free(cc1);
#ifdef SUBOPT
   free(fM1);
#endif
   free(base_pair); free(Fmi);
   free(DMLi); free(DMLi1);free(DMLi2);
   init_length=0;
}

/*--------------------------------------------------------------------------*/


PRIVATE   int   *BP; /* contains the structure constrainsts: BP[i]
                      -1: | = base must be paired
                      -2: < = base must be paired with j<i
                      -3: > = base must be paired with j>i
                      -4: x = base must not pair
	    positive int: base is paired with int      */

float fold(char *string, char *structure)
{
  struct sect {
      int  i;
      int  j;
      int ml;
   } 
   sector[MAXSECTORS];   /* backtracking sectors */
   
   int   i, j, k, l, p, q, length, energy, new_c, new;
   int   fij, fi, fj, ci1j, cij1, ci1j1, max_separation;
   int   decomp, MLenergy, new_fML, ml;
   int   s, b, traced, mm;
   int   no_close, type, type_2, tt;
   int   bonus=0, bonus_cnt=0;

   length = (int) strlen(string);
   if (length>init_length) initialize_fold(length);
   
   BP = (int *)space(sizeof(int)*(length+2));
   if (fold_constrained) BP_calculate(structure,BP,length);

   encode_seq(string);
   
   max_separation = (int) ((1.-LOCALITY)*(double)(length-2)); /* not in use */

   for (j=1; j<=length; j++) {
      Fmi[j]=DMLi[j]=DMLi1[j]=DMLi2[j]=INF;
   }
   
   for (j = 1; j<=length; j++)
     for (i=(j>TURN?(j-TURN):1); i<j; i++) {
       c[indx[j]+i] = fML[indx[j]+i] = INF;
#ifdef SUBOPT
       fM1[indx[j]+i] = INF;
#endif
     }       

   if (noLonelyPairs) { /* mark isolated pairs */
     for (k=2; k<length-TURN-1; k++) 
       for (l=1; l<=2; l++) {
	 int ntype,otype=0;
	 i = k; j = i+TURN+l;
	 type = pair[S[i]][S[j]];
	 while ((i>1)&&(j<length)) {
	   ntype = pair[S[i-1]][S[j+1]];
	   if (type&&(!otype)&&(!ntype)) /* i.j can only form isolated pairs */
	     c[indx[j]+i] = FORBIDDEN;
	   else c[indx[j]+i] = 0;
	   otype =  type;
	   type  = ntype;
	   i--; j++;
	 }
       }
   } 
   
   for (i = length-TURN-1; i >= 1; i--) { /* i,j in [1..length] */
      
      for (j = i+TURN+1; j <= length; j++) {

	 bonus = 0;
	 type = pair[S[i]][S[j]];
	 if (noLonelyPairs && (c[indx[j]+i]==FORBIDDEN))
	   type=0;  /* don't allow this pair */

	 /* enforcing structure constraints */
	 if ((BP[i]==j) && (type==0)) type=7; /* nonstandard */
	 if ((BP[i]==j)||(BP[i]==-1)||(BP[i]==-2)) bonus -= BONUS;
	 if ((BP[j]==-1)||(BP[j]==-3)) bonus -= BONUS;
	 if ((BP[i]==-4)||(BP[j]==-4)) type=0;
	 	 
	 no_close = (((type==3)||(type==4))&&no_closingGU&&(bonus==0));
		 
	 if (j-i-1 > max_separation) type = 0;  /* forces locality degree */

	 if (type) {   /* we have a pair */
	   int stackEnergy = INF;
	   /* hairpin ----------------------------------------------*/
		
	    if (no_close) new_c = FORBIDDEN;
	    else
	      new_c = HairpinE(i, j, type, string);
	    
	    /*--------------------------------------------------------
	      check for elementary structures involving more than one
	      closing pair.
	      --------------------------------------------------------*/
	    
	    for (p = i+1; p <= MIN2(j-2-TURN,i+MAXLOOP+1) ; p++) {
	      int minq = j-i+p-MAXLOOP-2;
	      if (minq<p+1+TURN) minq = p+1+TURN;
	      for (q = minq; q < j; q++) {
		type_2 = pair[S[p]][S[q]];

		if ((BP[p]==q) && (type_2==0)) type_2=7; /* nonstandard */

		if (type_2==0) continue;

		if (no_closingGU) 
		  if (no_close||(type_2==3)||(type_2==4))
		    if ((p>i+1)||(q<j-1)) continue;  /* continue unless stack */

#if 1
		energy = LoopEnergy(p-i-1, j-q-1, type, type_2,
				    S1[i+1], S1[j-1], S1[p-1], S[q+1]);
#else
		/* duplicated code is faster than function call */
		
#endif
		new_c = MIN2(energy+c[indx[q]+p], new_c);
		if ((p==i+1)&&(j==q+1)) stackEnergy = energy; /* remember stack energy */
	    
	      } /* end q-loop */
	    } /* end p-loop */

	    /* multi-loop decomposition ------------------------*/

	    if (!no_close) {
	      decomp = DMLi1[j-1];
	      if (dangles) {
		tt = rtype[type]; if ((tt==0)&&(bonus!=0)) tt=7;
		if (dangles==2) /* double dangles */
		  decomp += dangle5[tt][S1[j-1]]+dangle3[tt][S1[i+1]];
		else {          /* normal dangles */
		  decomp = MIN2(DMLi2[j-1]+dangle3[tt][S1[i+1]]+MLbase, decomp);
		  decomp = MIN2(DMLi1[j-2]+dangle5[tt][S1[j-1]]+MLbase, decomp);
		  decomp = MIN2(DMLi2[j-2]+dangle5[tt][S1[j-1]]+
				dangle3[tt][S1[i+1]] + 2*MLbase, decomp);
		}
	      }

	      MLenergy = MLclosing+MLintern[type]+decomp;
	      
	      new_c = MLenergy < new_c ? MLenergy : new_c;
	    }

	    new_c = MIN2(new_c, cc1[j-1]+stackEnergy);
	    cc[j] = new_c + bonus;
	    if (noLonelyPairs)
	      c[indx[j]+i] = cc1[j-1]+stackEnergy+bonus;
	    else
	      c[indx[j]+i] = cc[j];

	 } /* end >> if (pair) << */
	 
	 else c[indx[j]+i] = INF;

	 /* free ends ? -----------------------------------------*/

	 new_fML = fML[indx[j]+i+1]+MLbase;
	 new_fML = MIN2(fML[indx[j-1]+i]+MLbase, new_fML);
	 energy = c[indx[j]+i]+MLintern[type];
	 if (dangles==2) {  /* double dangles */
	   if (i>1)      energy += dangle5[type][S1[i-1]];
	   if (j<length) energy += dangle3[type][S1[j+1]];
	 }
	 new_fML = MIN2(energy, new_fML);
#ifdef SUBOPT
	  fM1[indx[j]+i] = MIN2(fM1[indx[j-1]+i] + MLbase, energy);
#endif
	 if (dangles==1) {  /* normal dangles */
	    tt = pair[S[i+1]][S[j]]; if ((tt==0)&&(BP[i+1]==j)) tt=7;
	    new_fML = MIN2(c[indx[j]+i+1]+dangle5[tt][S1[i]]
			   +MLintern[tt]+MLbase,new_fML);
	    tt = pair[S[i]][S[j-1]]; if ((tt==0)&&(BP[i]==j-1)) tt=7;
	    new_fML = MIN2(c[indx[j-1]+i]+dangle3[tt][S1[j]]
			   +MLintern[tt]+MLbase, new_fML);
	    tt = pair[S[i+1]][S[j-1]]; if ((tt==0)&&(BP[i+1]==j-1)) tt=7;
	    new_fML = MIN2(c[indx[j-1]+i+1]+dangle5[tt][S1[i]]+
			   dangle3[tt][S1[j]]+MLintern[tt]+2*MLbase, new_fML);
	 }
		 
	 /* modular decomposition -------------------------------*/

	 for (decomp = INF, k = i+1+TURN; k <= j-2-TURN; k++)
	    decomp = MIN2(decomp, Fmi[k]+fML[indx[j]+k+1]);

	 DMLi[j] = decomp;               /* store for use in ML decompositon */
	 new_fML = MIN2(new_fML,decomp);
	 
	 fML[indx[j]+i] = Fmi[j] = new_fML;     /* substring energy */
	 
      }

      {
	int *FF; /* rotate the auxilliary arrays */
	FF = DMLi2;
	DMLi2 = DMLi1;
	DMLi1 = DMLi;
	DMLi = FF;
	FF = cc1; cc1=cc; cc=FF;
	for (j=1; j<=length; j++) { cc[j]=Fmi[j]=DMLi[j]=INF; }
      }
   }

   /* calculate energies of 5' and 3' fragments */
   
   f5[TURN+1]=0;
   for (j=TURN+2; j<=length; j++) {
      f5[j] = f5[j-1];
      type=pair[S[1]][S[j]]; if ((type==0)&&(BP[1]==j)) type=7;
      if (type) {
	energy = c[indx[j]+1];
	if (type>2) energy += TerminalAU;
	if ((dangles==2)&&(j<length))  /* double dangles */
	  energy += dangle3[type][S1[j+1]];
	f5[j] = MIN2(f5[j], energy);
      }
      type=pair[S[1]][S[j-1]]; if ((type==0)&&(BP[1]==j-1)) type=7;
      if ((type)&&(dangles==1)) {
	energy = c[indx[j-1]+1]+dangle3[type][S1[j]];
	if (type>2) energy += TerminalAU;
	f5[j] = MIN2(f5[j], energy);
      }
      for (i=j-TURN-1; i>1; i--) {
	 type = pair[S[i]][S[j]]; if ((type==0)&&(BP[i]==j)) type=7;
	 if (type) {
	   energy = f5[i-1]+c[indx[j]+i];
	   if (type>2) energy += TerminalAU;
	   if (dangles==2) {
	     energy += dangle5[type][S1[i-1]];
	     if (j<length) energy += dangle3[type][S1[j+1]];
	   }
	   f5[j] = MIN2(f5[j], energy);
	   if (dangles==1) {
	     energy = f5[i-2]+c[indx[j]+i]+dangle5[type][S1[i-1]];
	     if (type>2) energy += TerminalAU;
	     f5[j] = MIN2(f5[j], energy);
	   }
	 }
	 type = pair[S[i]][S[j-1]]; if ((type==0)&&(BP[i]==j-1)) type=7;
	 if ((type)&&(dangles==1)) {
	   energy = c[indx[j-1]+i]+dangle3[type][S1[j]];
	   if (type>2) energy += TerminalAU;
	   f5[j] = MIN2(f5[j], f5[i-1]+energy);
	   f5[j] = MIN2(f5[j], f5[i-2]+energy+dangle5[type][S1[i-1]]);
	 }
      }
   }

#if 0
   for (i=length-TURN-1; i>0; i--) {
      f3[i] = f3[i+1];
      for (j=i+TURN+1; j<length; j++) {
	 type = pair[S[i]][S[j]]; if ((type==0)&&(BP[i]==j)) type=7;
	 if (type) {
	    f3[i] = MIN2(f3[i], c[indx[j]+i] + f3[j+1]);
	    if(dangles)
	       f3[i] = MIN2(f3[i], c[indx[j]+i] + f3[j+2] +dangle3[type][S1[j+1]]);
	 }
	 type = pair[S[i+1]][S[j]]; if ((type==0)&&(BP[i+1]==j)) type=7;
	 if((type)&&(dangles)) {
	    f3[i] = MIN2(f3[i], c[indx[j]+i+1] + f3[j+1] +dangle5[type][S1[i]]);
	    f3[i] = MIN2(f3[i], c[indx[j]+i+1] + f3[j+2] +
			 dangle5[type][S1[i]] + dangle3[type][S1[j+1]]);
	 }
      }
      f3[i] = MIN2(f3[i], c[indx[length]+i]);
      type=pair[S[i+1]][S[length]]; if ((type==0)&&(BP[i+1]==j)) type=7;
      if ((type)&&(dangles))
	 f3[i] = MIN2(f3[i], c[indx[length]+i+1] +dangle5[type][S1[i]]);
   }
   
   if (f3[1]!=f5[length])
      fprintf(stderr, "f3[1]!=f5[n]! %d  %d\n",f3[1],f5[length]);
#endif
   
   /*------------------------------------------------------------------
     trace back through the "c", "f5" and "fML" arrays to get the
     base pairing list. No search for equivalent structures is done.
     This inverts the folding procedure, hence it's very fast.
     ------------------------------------------------------------------*/

   ci1j = cij1 = ci1j1 = INF;
   b = 0;
   s = 1;
   sector[s].i = 1;
   sector[s].j = length;
   sector[s].ml = (backtrack_type=='M') ? 1 : 0;
   
   do {
     int cij;
     int canonical = 1;     /* (i,j) closes a canonical structure */
     i  = sector[s].i;
     j  = sector[s].j;
     ml = sector[s--].ml;   /* ml is a flag indicating if backtracking is to 
			       occur in the fML- (1) or in the f-array (0) */
     if ((i>1)&&(!ml))
       nrerror("Error while backtracking");
      
      if (((j-i+1)==length)&&(backtrack_type=='C')) {
	 base_pair[++b].i = i;
	 base_pair[b].j   = j;
	 goto repeat1;
      }
      
      if (j < i+TURN+1)
	 continue;

      if (!ml) { /* find outermost base pairs */
	 fij = f5[j];
	 if (fij == f5[j-1]) {
	    sector[++s].i = i;
	    sector[s].j   = j-1;
	    sector[s].ml  = 0;
	    continue;
	 } else {
	   int jj=0;
	    for (k=j-TURN-1,traced=0; k>1; k--) {
	       jj = k-1;
	       type = pair[S[k]][S[j-1]]; if ((type==0)&&(BP[k]==j-1)) type=7;
	       if((type)&&(dangles==1)) {
		 int cc;
		 cc = c[indx[j-1]+k]+dangle3[type][S1[j]];
		 if (type>2) cc += TerminalAU; 
		 if (f5[j] == f5[k-1] + cc)
		   traced=j-1;
		 if (f5[j] == f5[k-2] + cc + dangle5[type][S1[k-1]]) {
		   traced=j-1; jj=k-2;
		 }
	       }
	       type = pair[S[k]][S[j]]; if ((type==0)&&(BP[k]==j)) type=7;
	       if (type) {
		 int en, cc;
		 cc = (type>2)? c[indx[j]+k]+TerminalAU :c[indx[j]+k]; 
		 en = f5[k-1]+cc;
		 if (dangles==2) {
		   en += dangle5[type][S1[k-1]];
		   if (j<length) en += dangle3[type][S1[j+1]];
		 }
		 if (f5[j] == en) traced=j;
		 if (dangles==1)
		   if (f5[j] == f5[k-2]+cc+dangle5[type][S1[k-1]]) {
		     traced=j; jj=k-2;
		   }
	       }
	       if (traced) break;
	    }
	    if (!traced) {
	      int cc;
	      k=1; jj=k-1;
	      type=pair[S[1]][S[j]]; if ((type==0)&&(BP[1]==j)) type=7;
	      if (type) {
		cc = c[indx[j]+1]; if (type>2) cc += TerminalAU;
		if (f5[j] == cc) traced=j;
		if ((dangles==2)&&(j<length)) {
		  if (f5[j] == cc + dangle3[type][S1[j+1]]) traced=j;
		}
	      }
	      if (dangles==1) {
		type=pair[S[1]][S[j-1]];if ((type==0)&&(BP[1]==j-1)) type=7;
		if (type) {
		  cc = c[indx[j-1]+1]+dangle3[type][S1[j]];
		  if (type>2) cc += TerminalAU;
		  if (f5[j] == cc) traced=j-1;
		}
	      }
	    }
	    if (!traced) nrerror("backtrack failed in f5");
	    sector[++s].i = 1;
	    sector[s].j   = jj;
	    sector[s].ml  = 0;

	    i=k; j=traced;
	    base_pair[++b].i = i;
	    base_pair[b].j   = j;
	    goto repeat1;
	 }
	 
      } else { /* trace back in fML array */
	 fij = fML[indx[j]+i];
	 fj  = fML[indx[j]+i+1]+MLbase;
	 fi  = fML[indx[j-1]+i]+MLbase;
	 tt  = pair[S[i]][S[j]]; if ((tt==0)&&(BP[i]==j)) tt=7;
	 cij = c[indx[j]+i] + MLintern[tt];
	 if (dangles==2) {       /* double dangles */
	   if (i>1)      cij += dangle5[tt][S1[i-1]];
	   if (j<length) cij += dangle3[tt][S1[j+1]];
	 }
	 else if (dangles==1) {  /* normal dangles */
	    tt = pair[S[i+1]][S[j]]; if ((tt==0)&&(BP[i+1]==j)) tt=7;
	    ci1j= c[indx[j]+i+1] + dangle5[tt][S1[i]] + MLintern[tt]+MLbase;
	    tt = pair[S[i]][S[j-1]]; if ((tt==0)&&(BP[i]==j-1)) tt=7;
	    cij1= c[indx[j-1]+i] + dangle3[tt][S1[j]] + MLintern[tt]+MLbase;
	    tt = pair[S[i+1]][S[j-1]]; if ((tt==0)&&(BP[i+1]==j-1)) tt=7;
	    ci1j1=c[indx[j-1]+i+1] + dangle5[tt][S1[i]] + dangle3[tt][S1[j]]
	       +  MLintern[tt] + 2*MLbase;
	 }
	 if (fij == fj) {
	    sector[++s].i = i+1;
	    sector[s].j   = j;
	    sector[s].ml  = ml;
	    continue;
	 }
	 else if (fij == fi) {
	    sector[++s].i = i;
	    sector[s].j   = j-1;
	    sector[s].ml  = ml;
	    continue;
	 }
	 else {
	    if ((fij==cij)||(fij==ci1j)||(fij==cij1)||(fij==ci1j1)) {
	       if (fij==ci1j) i++;
	       else if (fij==cij1) j--;
	       else if (fij==ci1j1) {i++; j--;}
	       base_pair[++b].i = i;
	       base_pair[b].j   = j;
	       goto repeat1;
	    } 
	 }
      
	 for (k = i+1+TURN; k <= j-2-TURN; k++) {
	    if (fML[indx[j]+i] == (fML[indx[k]+i]+fML[indx[j]+k+1])) {
	       sector[++s].i = i;
	       sector[s].j   = k;
	       sector[s].ml  = ml;
	       sector[++s].i = k+1;
	       sector[s].j   = j;
	       sector[s].ml  = ml;
	       break;
	    }
	 }
      }
      
      if (k>j-2-TURN) fprintf(stderr,"backtrack failed\n%s", string);
      continue;
      
    repeat1:
      
      /*----- begin of "repeat:" -----*/
      if (canonical)  cij = c[indx[j]+i];

      type = pair[S[i]][S[j]];
      if ((BP[i]==j) && (type==0)) type=7; /* nonstandard */
      
      bonus = 0;

      if ((BP[i]==j)||(BP[i]==-1)||(BP[i]==-2)) bonus -= BONUS;
      if ((BP[j]==-1)||(BP[j]==-3)) bonus -= BONUS;
      if ((BP[i]==-4)||(BP[j]==-4)) type=0;
        
      if (noLonelyPairs) 
	if (cij == c[indx[j]+i]) {
	  /* (i.j) closes canonical structures, thus
	     (i+1.j-1) must be a pair                */
	  cij -= stack[type][pair[S[i+1]][S[j-1]]] + bonus;
	  base_pair[++b].i = i+1;
	  base_pair[b].j   = j-1;
	  i++; j--;
	  canonical=0;
	  goto repeat1;
	}
      canonical = 1;
 

      no_close = (((type==3)||(type==4))&&no_closingGU&&(bonus==0));
      if (no_close) {
	if (cij == FORBIDDEN) continue;
      } else
	if (cij == HairpinE(i, j, type, string)+bonus)
	  continue;
   
      for (p = i+1; p <= MIN2(j-2-TURN,i+MAXLOOP+1); p++) {
	int minq = j-i+p-MAXLOOP-2;
	if (minq<p+1+TURN) minq = p+1+TURN;
	for (q = j-1; q >= minq; q--) {
	  
	  type_2 = pair[S[p]][S[q]];
	  if ((BP[p]==q) && (type_2==0)) type_2=7; /* nonstandard */
	  
	  if (type_2==0) continue;
	  
	  if (no_closingGU) 
	    if (no_close||(type_2==3)||(type_2==4))
	      if ((p>i+1)||(q<j-1)) continue;  /* continue unless stack */
	  
	  /* energy = oldLoopEnergy(i, j, p, q, type, type_2); */
	  energy = LoopEnergy(p-i-1, j-q-1, type, type_2,
			      S1[i+1], S1[j-1], S1[p-1], S1[q+1]);
	  
	  new = energy+c[indx[q]+p]+bonus;
	  traced = (cij == new);
	  if (traced) {
	    base_pair[++b].i = p;
	    base_pair[b].j   = q;
	    i = p, j = q;
	    goto repeat1;
	  }
	}
      }
      
      /* end of repeat: --------------------------------------------------*/


      tt = pair[S[j]][S[i]]; if ((tt==0)&&(BP[i]==j)) tt=7;
      mm = bonus+MLclosing+MLintern[tt];

      for (k = i+2+TURN; k <= j-3-TURN; k++) {
	int en;
	en = fML[indx[k]+i+1]+fML[indx[j-1]+k+1]+mm;
	if (dangles==2) /* double dangles */
	  en += dangle5[tt][S1[j-1]] + dangle3[tt][S1[i+1]];
	 if (cij == en) {
	    sector[++s].i = i+1;
	    sector[s+1].j = j-1;
	    break;
	 }
	 if (dangles==1) { /* normal dangles */
	    if (cij == (fML[indx[k]+i+2]+fML[indx[j-1]+k+1]+mm+
			dangle3[tt][S1[i+1]]+MLbase)) {
	       sector[++s].i = i+2;
	       sector[s+1].j = j-1;
	       break;
	    }
	    if (cij == (fML[indx[k]+i+1]+fML[indx[j-2]+k+1]+mm+
			dangle5[tt][S1[j-1]]+MLbase)) {
	       sector[++s].i = i+1;
	       sector[s+1].j = j-2;
	       break;
	    }
	    if (cij == (fML[indx[k]+i+2]+fML[indx[j-2]+k+1]+mm+
			dangle3[tt][S1[i+1]]+dangle5[tt][S1[j-1]]+
			2*MLbase)) {
	       sector[++s].i = i+2;
	       sector[s+1].j = j-2;
	       break;
	    }
	 }
      }
      sector[s].ml  = 1; 
      sector[s].j   = k;
      sector[++s].i = k+1;
      sector[s].ml  = 1; 

      if (k>j-3-TURN) fprintf(stderr, "backtracking failed\n%s\n", string);
   }
   while (s > 0);

   base_pair[0].i = b;    /* save the total number of base pairs */

#ifdef PAREN
   parenthesis_structure(structure, length);
#else
   letter_structure(structure, length);
#endif

   bonus=0;
   bonus_cnt = 0;
   for(l=1;l<=length;l++) {
      if((BP[l]<0)&&(BP[l]>-4)) {
	 bonus_cnt++;
	 if((BP[l]==-3)&&(structure[l-1]==')')) bonus++;
	 if((BP[l]==-2)&&(structure[l-1]=='(')) bonus++;
	 if((BP[l]==-1)&&(structure[l-1]!='.')) bonus++;
      }
      
      if(BP[l]>l) {
	 bonus_cnt++;
	 for(i=1;i<=b;i++)
	    if((l==base_pair[i].i)&&(BP[l]==base_pair[i].j)) bonus++;
      }
   }

   if (bonus_cnt>bonus) fprintf(stderr,"\ncould not enforce all constraints\n");
   bonus*=BONUS;

#ifndef SUBOPT
   free(S); free(S1);
   free(BP);
#endif   
   
   f5[length] += bonus;      

   if (backtrack_type=='C')
      return (float) c[indx[length]+1]/100.;
   else if (backtrack_type=='M')
      return (float) fML[indx[length]+1]/100.;
   else
      return (float) f5[length]/100.;
}

/*---------------------------------------------------------------------------*/

INLINE int HairpinE(int i, int j, int type, const char *string) {
  int energy;
  energy = (j-i-1 <= 30) ? hairpin[j-i-1] :
    hairpin[30]+(int)(lxc*log((j-i-1)/30.));
  if (tetra_loop)
    if (j-i-1 == 4) { /* check for tetraloop bonus */
      char tl[7]={0}, *ts;
      strncpy(tl, string+i-1, 6);
      if ((ts=strstr(Tetraloops, tl))) 
	energy += TETRA_ENERGY[(ts-Tetraloops)/7];
    }
  if (j-i-1 == 3) {
    char tl[6]={0,0,0,0,0,0}, *ts;
    strncpy(tl, string+i-1, 5);
    if ((ts=strstr(Triloops, tl))) 
      energy += Triloop_E[(ts-Triloops)/6];
    if (type>2)  /* neither CG nor GC */
      energy += TerminalAU; /* penalty for closing AU GU pair */
  }
  else  /* no mismatches for tri-loops */
    energy += mismatchH[type][S1[i+1]][S1[j-1]];
  
  return energy;
}

/*---------------------------------------------------------------------------*/

INLINE int oldLoopEnergy(int i, int j, int p, int q, int type, int type_2) {
  /* compute energy of degree 2 loop (stack bulge or interior) */
  int n1, n2, m, energy;
  n1 = p-i-1;
  n2 = j-q-1;

  if (n1>n2) { m=n1; n1=n2; n2=m; } /* so that n2>=n1 */

  if (n2 == 0)
    energy = stack[type][type_2];   /* stack */
  
  else if (n1==0) {                  /* bulge */
    energy = (n2<=MAXLOOP)?bulge[n2]:
      (bulge[30]+(int)(lxc*log(n2/30.)));

#if STACK_BULGE1
    if (n2==1) energy+=stack[type][type_2];
#endif
  } else {                           /* interior loop */
    int rtype2;
    rtype2  = rtype[type_2];

    if ((n1+n2==2)&&(james_rule))
      /* special case for loop size 2 */
      energy = int11[type][rtype2][S1[i+1]][S1[j-1]];
    else {
      energy = (n1+n2<=MAXLOOP)?(internal_loop[n1+n2]):
	(internal_loop[30]+(int)(lxc*log((n1+n2)/30.)));
      
#if NEW_NINIO
      energy += MIN2(MAX_NINIO, (n2-n1)*F_ninio[2]);
#else
      m       = MIN2(4, n1);
      energy += MIN2(MAX_NINIO,((n2-n1)*F_ninio[m]));
#endif
      energy += mismatchI[type][S1[i+1]][S1[j-1]]+
	mismatchI[rtype2][S1[q+1]][S1[p-1]];
    }
  }
  return energy;
}

/*--------------------------------------------------------------------------*/

INLINE int LoopEnergy(int n1, int n2, int type, int type_2,
                       int si1, int sj1, int sp1, int sq1) {
  /* compute energy of degree 2 loop (stack bulge or interior) */
  int nl, ns, energy;

  
  if (n1>n2) { nl=n1; ns=n2;}
  else {nl=n2; ns=n1;}

  if (nl == 0)
    return stack[type][type_2];    /* stack */
  
  if (ns==0) {                       /* bulge */
    energy = (nl<=MAXLOOP)?bulge[nl]:
      (bulge[30]+(int)(lxc*log(n2/30.)));
    if (nl==1) energy += stack[type][type_2];
    return energy;
  }
  else {                             /* interior loop */
    int rtype2;
    rtype2  = rtype[type_2];

    if (ns==1) { 
      if (nl==1)                     /* 1x1 loop */
	return int11[type][rtype2][si1][sj1];
      if (nl==2) {                   /* 2x1 loop */
	if (n1==1)
	  energy = int21[type][rtype2][si1][sq1][sj1];
	else
	  energy = int21[rtype2][type][sq1][si1][sp1];
	return energy;
      }
    }
    else if (n1==2 && n2==2)         /* 2x2 loop */
      return int22[type][rtype2][si1][sp1][sq1][sj1];
    { /* generic interior loop (no else here!)*/
      energy = (n1+n2<=MAXLOOP)?(internal_loop[n1+n2]):
	(internal_loop[30]+(int)(lxc*log((n1+n2)/30.)));
      
      energy += MIN2(MAX_NINIO, (nl-ns)*F_ninio[2]);
      
      energy += mismatchI[type][si1][sj1]+
	mismatchI[rtype2][sq1][sp1];
    }
  }
  return energy;
}


/*---------------------------------------------------------------------------*/

PRIVATE void encode_seq(char *sequence) {
  unsigned int i,l;

  l = strlen(sequence);
  S = (short *) space(sizeof(short)*(l+1));
  S1= (short *) space(sizeof(short)*(l+1));
  /* S1 exists only for the special X K and I bases and energy_set!=0 */
  S[0] = S1[0] = (short) l;
  
  for (i=1; i<=l; i++) { /* make numerical encoding of sequence */
    S[i]= (short) encode_char(toupper(sequence[i-1]));
    S1[i] = alias[S[i]];   /* for mismatches of nostandard bases */
  }
}

/*---------------------------------------------------------------------------*/

PRIVATE void letter_structure(char *structure, int length)
{
  int n, k, x, y;
  
  for (n = 0; n <= length-1; structure[n++] = ' ') ;
  structure[length] = '\0';
  
  for (n = 0, k = 1; k <= base_pair[0].i; k++) {
    y = base_pair[k].j;
    x = base_pair[k].i;
    if (x-1 > 0 && y+1 <= length) {
      if (structure[x-2] != ' ' && structure[y] == structure[x-2]) {
	structure[x-1] = structure[x-2];
	structure[y-1] = structure[x-1];
	continue;
      }
    }
    if (structure[x] != ' ' && structure[y-2] == structure[x]) {
      structure[x-1] = structure[x];
      structure[y-1] = structure[x-1];
      continue;
    }
    n++;
    structure[x-1] = alpha[n-1];
    structure[y-1] = alpha[n-1];
  }
}

/*---------------------------------------------------------------------------*/

PRIVATE void parenthesis_structure(char *structure, int length)
{
  int n, k;
  
  for (n = 0; n <= length-1; structure[n++] = '.') ;
  structure[length] = '\0';

  for (k = 1; k <= base_pair[0].i; k++) {
    structure[base_pair[k].i-1] = '(';
    structure[base_pair[k].j-1] = ')';
  }
}
/*---------------------------------------------------------------------------*/

PRIVATE void scale_parameters(void)
{
   unsigned int i,j,k,l;
   double tempf;

   tempf = ((temperature+K0)/Tmeasure);
   for (i=0; i<31; i++) 
      hairpin[i] = (int) hairpin37[i]*(tempf);
   for (i=0; i<=MIN2(30,MAXLOOP); i++) {
      bulge[i] = (int) bulge37[i]*tempf;
      internal_loop[i]= (int) internal_loop37[i]*tempf;
   }
   lxc = lxc37*tempf;
   for (; i<=MAXLOOP; i++) {
      bulge[i] = bulge[30]+(int)(lxc*log((double)(i)/30.));
      internal_loop[i] = internal_loop[30]+(int)(lxc*log((double)(i)/30.));
   }
   for (i=0; i<5; i++)
      F_ninio[i] = (int) F_ninio37[i]*tempf;
   
   for (i=0; (i*7)<strlen(Tetraloops); i++) 
     TETRA_ENERGY[i] = TETRA_ENTH37 - (TETRA_ENTH37-TETRA_ENERGY37[i])*tempf;
   for (i=0; (i*5)<strlen(Triloops); i++) 
     Triloop_E[i] =  Triloop_E37[i];
   
   MLbase = ML_BASE37*tempf;
   for (i=0; i<=NBPAIRS; i++) { /* includes AU penalty */
     MLintern[i] = ML_intern37*tempf;
     MLintern[i] +=  (i>2)?TerminalAU:0;
   }
   MLclosing = ML_closing37*tempf;

   /* stacks    G(T) = H - [H - G(T0)]*T/T0 */
   for (i=0; i<=NBPAIRS; i++)
      for (j=0; j<=NBPAIRS; j++)
	stack[i][j] = enthalpies[i][j] -
	  (enthalpies[i][j] - stack37[i][j])*tempf;

   /* mismatches */
   for (i=0; i<=NBPAIRS; i++)
     for (j=0; j<5; j++)
       for (k=0; k<5; k++) {
	 mismatchI[i][j][k] = mism_H[i][j][k] -
	   (mism_H[i][j][k] - mismatchI37[i][j][k])*tempf;
	 mismatchH[i][j][k] = mism_H[i][j][k] -
	   (mism_H[i][j][k] - mismatchH37[i][j][k])*tempf;
	 mismatchM[i][j][k] = mism_H[i][j][k] -
	   (mism_H[i][j][k] - mismatchM37[i][j][k])*tempf;
       }
   
   /* dangles */
   for (i=0; i<=NBPAIRS; i++)
     for (j=0; j<5; j++) {
       int dd;
       dd = dangle5_H[i][j] - (dangle5_H[i][j] - dangle5_37[i][j])*tempf; 
       dangle5[i][j] = (dd>0) ? 0 : dd;  /* must be <= 0 */
       dd = dangle3_H[i][j] - (dangle3_H[i][j] - dangle3_37[i][j])*tempf;
       dangle3[i][j] = (dd>0) ? 0 : dd;  /* must be <= 0 */
     }
   /* interior 1x1 loops */
   for (i=0; i<=NBPAIRS; i++)
     for (j=0; j<=NBPAIRS; j++)
       for (k=0; k<5; k++)
	 for (l=0; l<5; l++) 
	   int11[i][j][k][l] = int11_H[i][j][k][l] -
	     (int11_H[i][j][k][l] - int11_37[i][j][k][l])*tempf;

   /* interior 2x1 loops */
   for (i=0; i<=NBPAIRS; i++)
     for (j=0; j<=NBPAIRS; j++)
       for (k=0; k<5; k++)
	 for (l=0; l<5; l++) {
	   int m;
	   for (m=0; m<5; m++)
	     int21[i][j][k][l][m] = int21_H[i][j][k][l][m] -
	       (int21_H[i][j][k][l][m] - int21_37[i][j][k][l][m])*tempf;
	 }
   /* interior 2x2 loops */
   for (i=0; i<=NBPAIRS; i++)
     for (j=0; j<=NBPAIRS; j++)
       for (k=0; k<5; k++)
	 for (l=0; l<5; l++) {
	   int m,n;
	   for (m=0; m<5; m++)
	     for (n=0; n<5; n++)	     
	       int22[i][j][k][l][m][n] = int22_H[i][j][k][l][m][n] -
		 (int22_H[i][j][k][l][m][n]-int22_37[i][j][k][l][m][n])*tempf;
	 }
}

/*---------------------------------------------------------------------------*/
	   
PUBLIC void update_fold_params(void)
{
   scale_parameters();
   make_pair_matrix();
   if (init_length < 0) init_length=0;
}

/*---------------------------------------------------------------------------*/

float energy_of_struct(char *string, char *structure)
{
   int   energy;
   short *ss, *ss1;

   if (init_length<0) update_fold_params();
   
   if (strlen(structure)!=strlen(string))
      nrerror("energy_of_struct: string and structure have unequal length");

   /* save the S and S1 pointers in case they were already in use */
   ss = S; ss1 = S1;
   encode_seq(string);
   
   pair_table = make_pair_table(structure);

   energy = energy_of_struct_pt(string, pair_table, S, S1);
   
   free(pair_table);
   free(S); free(S1);
   S=ss; S1=ss1;
   return  (float) energy/100.;
}

int energy_of_struct_pt(char *string, short * ptable, short *s, short *s1) {
  /* auxiliary function for kinfold,
     for most purposes call energy_of_struct instead */
  
  int   i, length, energy;
  int   type, j, lastd, ee, ld3;
  
  energy=lastd=ld3=0;
  pair_table = ptable;
  S = s;
  S1 = s1;
  
  length = S[0];
  for (i=1; i<=length; i++) {
    if (pair_table[i]==0) {
      if (backtrack_type=='M') energy+=MLbase;
      continue;
    }
    j=pair_table[i];
    type = pair[S[i]][S[j]]; if (type==0) type=7;
    if (type>2) energy += TerminalAU;
    if (dangles) {      /* dangling end contributions */
      if ((i>1)&&((pair_table[i-1]==0)||(dangles==2))) {
	ee = dangle5[type][S1[i-1]];               /* 5' dangle */
	if ((i-1==lastd)&&(dangles!=2)) ee -= ld3;     /* subtract 3' */
	energy += (ee<0)?ee:0;
      }
      if ((j<length)&&((pair_table[j+1]==0)||(dangles==2))) {
	ld3 = dangle3[type][S1[j+1]]; /* 3'dangle */
	energy += ld3;
	lastd = j+1;                             /* store last 3'dangle */
      }
    }
    energy += stack_energy(i, string);          
    
    if (backtrack_type=='M') energy+=MLintern[pair[S[i]][S[pair_table[i]]]];
    i=pair_table[i];
  }
  return energy;
}

/*---------------------------------------------------------------------------*/
PRIVATE int stack_energy(int i, char *string)  
{
  /* calculate energy of substructure enclosed by (i,j) */
  int energy = 0;
  int j, p, q, type;

  j=pair_table[i];
  type = pair[S[i]][S[j]];
  if (type==0) {
    type=7;
    fprintf(stderr,"WARNING: bases %d and %d (%c%c) can't pair!\n", i, j,
            string[i-1],string[j-1]);
  }
   
  p=i; q=j;
  while (p<q) { /* process all stacks and interior loops */
    int type_2, ee;
    while (pair_table[++p]==0);
    while (pair_table[--q]==0);
    if ((pair_table[q]!=(short)p)||(p>q)) break;
    type_2 = pair[S[p]][S[q]];
    if (type_2==0) {
      type_2=7;
      fprintf(stderr,"WARNING: bases %d and %d (%c%c) can't pair!\n", p, q,
              string[p-1],string[q-1]);
    }
    /* energy += LoopEnergy(i, j, p, q, type, type_2); */
    ee = LoopEnergy(p-i-1, j-q-1, type, type_2,
		    S1[i+1], S1[j-1], S1[p-1], S1[q+1]);
    energy += ee;    
    if (eos_debug)
      printf("Interior loop (%3d,%3d) %c%c; (%3d,%3d) %c%c: %5d\n",
	     i,j,string[i-1],string[j-1],p,q,string[p-1],string[q-1], ee);
    i=p; j=q; type = type_2;
  } /* end while */

  /* p,q don't pair must have found hairpin or multiloop */
      
  if (p>q) {                       /* hair pin */
    energy += HairpinE(i, j, type, string);
    if (eos_debug)
      printf("Hairpin  loop (%3d,%3d) %c%c              : %5d\n",
	     i,j,string[i-1],string[j-1],HairpinE(i, j, type, string));
    return energy;
  }
   
  /* (i,j) is exterior pair of multiloop */
  while (p<j) {
    /* add up the contributions of the enclosed substructures */
    energy += stack_energy(p, string);
    p = pair_table[p]; 
    while (pair_table[++p]==0);
  }
  energy += ML_Energy(i);
  if (eos_debug)
    printf("Multi    loop (%3d,%3d) %c%c              : %5d\n",
	     i,j,string[i-1],string[j-1],ML_Energy(i));
  return energy;
}

/*---------------------------------------------------------------------------*/

PRIVATE int ML_Energy(int i) {
  /* 
     since each helix can coaxially stack with at most one of its
     neighbors we need an auxiliarry variable  cx_energy
     which contains the best energy given that the last two pairs stack.
     energy  holds the best energy given the previous two pairs do not
     stack (i.e. the two current helices may stack)
     We don't allow the last helix to stack with the first, thus we have to
     walk around the Loop twice with two starting points and take the minimum
  */

  int energy, cx_energy, best_energy=INF;
  int i1, j, p, q, u, type, count;
 
  for (count=0; count<2; count++) { /* do it twice */
    int ld5=0;
    i1=i; p = i+1; j = pair_table[i];
    type = pair[S[j]][S[i]]; if (type==0) type=7;
    u=0;
    energy = 0; cx_energy=INF;
    do { /* walk around the multi-loop */
      int tt, new_cx = INF;
      
      while (pair_table[p]==0) p++;
            
      u += p-i1-1; 
      q  = pair_table[p];
      tt = pair[S[p]][S[q]]; if (tt==0) tt=7;

      /* FIXME: coax stack should not get TerminalAU penalty */
      energy += MLintern[tt];
      cx_energy += MLintern[tt];
      
      if (dangles) {
        int dang5, dang3, dang;
        dang5 = dangle5[tt][S1[p-1]];      /* 5' dangle of this pair */
        dang3 = dangle3[type][S1[i1+1]];   /* 3' dangle of previous pair */
        switch (p-i1-1) {
        case 0: /* adjacent helices */
          if (dangles==2) energy += dang3+dang5;
          else if (dangles==3) {
            new_cx = energy + stack[rtype[type]][tt] -ld5;
            energy = MIN2(energy, cx_energy);
            ld5=0;
          }
          break;
        case 1:
          dang = (dangles==2)?(dang3+dang5):MIN2(dang3, dang5);
          if (dangles==3) {
            energy = MIN2(energy + dang, cx_energy + dang5);
            new_cx = INF;  /* no coax stacking with mismatch for now */
            ld5 = dang - dang3;
          } else
            energy += dang;
          break;
        default:
          energy += dang5 +dang3;
          if (dangles==3) {
            energy = MIN2(energy, cx_energy + dang5);
            new_cx = INF;  /* no coax stacking possible */
            ld5 = dang5;
          }
        }
        type = tt;
      }
      if (dangles==3) cx_energy = new_cx;
      i1 = q; p=q+1;
    } while (q!=i);
    best_energy = MIN2(energy, best_energy); /* don't use cx_energy here */
    /* fprintf(stderr, "%6.2d\t", energy); */
    /* skip a helix and start again */
    while (pair_table[p]==0) p++;
    i = pair_table[p]; 
  }
  energy = best_energy;
  energy += MLclosing;
  /* logarithmic ML loop energy if logML */
  if (logML && (u>6))
    energy += 6*MLbase+(int)(lxc*log((double)u/6.));
  else
    energy += MLbase*u;
  /* fprintf(stderr, "\n"); */
  return energy;
}

/*---------------------------------------------------------------------------*/

PRIVATE void BP_calculate(char *structure, int *BP, int length)
{
   int i,j,ct;
   
   for(i=0;i<length;i++) {
      switch (structure[i]) {
       case '|': BP[i+1] = -1; break;
       case '<': BP[i+1] = -2; break;
       case '>': BP[i+1] = -3; break;
       case 'x': BP[i+1] = -4; break;
       case '(': 
	 ct = 1;
	 for(j=i+1;j<length;j++) {
	    if(structure[j]==')') ct--;
	    if(structure[j]=='(') ct++;
	    if(ct==0) {
	       BP[i+1]=j+1;
	       BP[j+1]=i+1;
	       break;
	    }
	 }
	 break;
      }
   }
}
