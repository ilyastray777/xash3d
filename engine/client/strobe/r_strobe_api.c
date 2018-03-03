/*
r_strobe_api.c - Software based strobing implementation

Copyright (C) 2018 * fuzun

For information:
	https://forums.blurbusters.com

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.
*/

#if !defined XASH_DEDICATED && !defined STROBE_DISABLED

#include <math.h>
#include <float.h>
#include "r_strobe_api.h"
#include "r_strobe_base_protected_.h"
#include "gl_local.h"
#include "client.h"


#define lossCalculator(x, y) (((x) - (y)) * 100.0 / (x)) // x : supposed y : current

convar_t	*r_strobe;
convar_t	*r_strobe_swapinterval;
convar_t	*r_strobe_debug;
convar_t	*r_strobe_cooldown;

static inline double func_helper_StandardDeviation(const double *data, int n)
{
	double mean = 0.0, sum_deviation = 0.0;
	int i;
	for (i = 0; i < n; ++i)
	{
		mean += data[i];
	}
	mean = mean / n;
	for (i = 0; i < n; ++i)
		sum_deviation += (data[i] - mean)*(data[i] - mean);
	return sqrt(sum_deviation / n);
}


static inline void GL_GenerateBlackFrame(void) // Generates partial or full black frame
{
	if (CL_IsInConsole()) // No strobing on the console
	{
		if (!vid_fullscreen->integer) // Disable when not fullscreen due to viewport problems
		{
			R_Set2DMode(false);
			return;
		}
		pglEnable(GL_SCISSOR_TEST);
		pglScissor(con_rect.x, (-con_rect.y) - (con_rect.h * 1.25), con_rect.w, con_rect.h); // Preview strobe setting on static
		pglClearColor(0.0f, 0.0f, 0.0f, 1.0f);
		pglClear(GL_COLOR_BUFFER_BIT);
		pglDisable(GL_SCISSOR_TEST);
	}
	else
	{
		//pglFlush(); // ?
		pglClearColor(0.0f, 0.0f, 0.0f, 1.0f);
		pglClear(GL_COLOR_BUFFER_BIT);
	}
}

static inline double func_helper_getCooldown(StrobeAPI_t *self)
{
	if ((double)abs(r_strobe_cooldown->integer) - self->protected->cdTimer <= (double)abs(r_strobe_cooldown->integer))
	{
		return ((double)abs(r_strobe_cooldown->integer) - self->protected->cdTimer);
	}
	else
	{
		return 0.0;
	}
}

static inline qboolean func_helper_isPhaseInverted(StrobeAPI_t *self)
{
	if (!!(self->protected->frameInfo & p_inverted))
		return true;
	else
		return false;
}

static inline qboolean func_helper_isNormal(StrobeAPI_t *self)
{
	if (!!(self->protected->frameInfo & f_normal))
		return true;
	else
		return false;
}

static inline qboolean func_helper_isPositive(StrobeAPI_t *self) // ...
{
	if (!!(self->protected->frameInfo & p_positive))
		return true;
	else
		return false;
}

static inline double func_helper_effectiveFPS(StrobeAPI_t *self)
{
	int strobeInterval = r_strobe->integer;
	double eFPS;
	if (strobeInterval > 0)
	{
		eFPS = (self->get.CurrentFPS(self)) / (strobeInterval + 1);
	}
	else if (strobeInterval < 0)
	{
		strobeInterval = abs(strobeInterval);
		eFPS = (self->get.CurrentFPS(self) * strobeInterval) / (strobeInterval + 1);
	}
	else
		eFPS = 0.0;
	return eFPS;
}

static inline void func_helper_GenerateDiffBar(StrobeAPI_t *self, char *src, int size, char type)
{
	char _barCounter = 0;
	int diff_NB = 0;
	int diff = 0;
	qboolean Neg = false;

	switch (type)
	{
	case(0): // Positive Difference
	{
		diff_NB = (self->protected->pNCounter - self->protected->pBCounter);

		if (self->protected->pCounter != 0)
			diff = round(abs(diff_NB) * 100 / self->protected->pCounter);

		break;
	}
	case(1): // Negative Difference
	{
		diff_NB = (self->protected->nNCounter - self->protected->nBCounter);

		if (self->protected->nCounter != 0)
			diff = round(abs(diff_NB) * 100 / self->protected->nCounter);

		break;
	}
	case(2): // Difference of difference
	{
		if (self->protected->nCounter != 0 && self->protected->pCounter != 0)
		{
			// FIX THE MESS!
			int a = (abs(self->protected->pNCounter - self->protected->pBCounter) * 100 / self->protected->pCounter);
			int b = (abs(self->protected->nNCounter - self->protected->nBCounter) * 100 / self->protected->nCounter);
			int x = ((int)(self->protected->pNCounter - self->protected->pBCounter) > 0);
			int y = ((int)(self->protected->nNCounter - self->protected->nBCounter) > 0);
			diff = abs((x ? a : (-a)) \
				- ((y ? b : (-b)))); // Min 0 Max 200
		}
		break;
	}
	default:
		break;
	}

	if (diff_NB < 0)
		Neg = true;

	Q_snprintf(src, size, "^3[");

	for (_barCounter = 0; _barCounter <= 20; ++_barCounter)
	{
		if (_barCounter == 10)
		{
			Q_strcat(src, "O");
		}
		else if (_barCounter < 10)
		{
			if (type == 2)
			{
				if (100 - (_barCounter * 11) <= diff / 2)
					Q_strcat(src, "^4=^3");
				else
					Q_strcat(src, "^3=");
			}
			else
			{
				if (Neg)
				{
					if (100 - (_barCounter * 11) <= diff)
						Q_strcat(src, "^4=^3");
					else
						Q_strcat(src, "^3=");
				}
				else
				{
					Q_strcat(src, "^3=");
				}
			}
		}
		else if (_barCounter > 10)
		{
			if (type == 2)
			{
				if (((_barCounter - 11) * 11) >= diff / 2)
					Q_strcat(src, "^3=");
				else
					Q_strcat(src, "^4=^3");
			}
			else
			{
				if (Neg)
				{
					Q_strcat(src, "^3=");
				}
				else
				{
					if (((_barCounter - 11) * 11) >= diff)
						Q_strcat(src, "^3=");
					else
						Q_strcat(src, "^4=^3");
				}
			}
		}
	}
	if (type == 2)
	{
		Q_strcat(src, va("] - %4d", diff));
	}
	else
	{
		Q_strcat(src, va("] - %4d%%", (Neg ? -diff : diff)));
	}
}


static inline int func_pwmsimulation_Frequency(StrobeAPI_t *self)
{
	return (int)round((1 / ((1.0f / self->get.CurrentFPS(self))*(abs(r_strobe->integer) + 1))));
}

static inline double func_pwmsimulation_DutyCycle(void)
{
	int strobeInterval = r_strobe->integer;
	return (((1.0f / (abs(strobeInterval) + 1)) * 100) * (strobeInterval < 0 ? -strobeInterval : 1));
}

static inline double func_pwmsimulation_PositivePhaseShift(StrobeAPI_t *self)
{
	return !!(self->protected->frameInfo & p_inverted) ? (1.0f / self->get.CurrentFPS(self)) * 1000 : 0.0f;
}

static inline double func_pwmsimulation_NegativePhaseShift(StrobeAPI_t *self)
{
	if (!!(self->protected->frameInfo & p_inverted))
		return abs(r_strobe->integer) * (1.0f / self->get.CurrentFPS(self)) * 1000;
	else
		return 0.0;
}

static inline double func_pwmsimulation_Period(StrobeAPI_t *self)
{
	return (((1.0f / self->get.CurrentFPS(self))*(abs(r_strobe->integer) + 1)) * 1000);
}

static inline double func_helper_GeometricMean(double x, double y)
{
	return sqrt(abs(x * y));
	/*
	int multiply = (x * y);
	if (multiply >= 0)
		return sqrt(multiply);
	else
		return sqrt(abs(multiply));
	*/
}

static inline double func_helper_ArithmeticMean(double x, double y)
{
	return (x + y) / 2;
}


static inline double func_brightnessreduction_ActualBrightnessReduction(StrobeAPI_t *self)
{
	double currentFPS = self->get.CurrentFPS(self);
	double effectiveFPS = self->Helpers.effectiveFPS(self);
	//return ((currentFPS - effectiveFPS) * 100.0 / currentFPS);
	return lossCalculator(currentFPS, effectiveFPS);
}

static inline double func_brightnessreduction_LogarithmicBrightnessReduction(StrobeAPI_t *self, double base)
{

	return lossCalculator( log(base), log(base * self->Helpers.effectiveFPS(self) / self->get.CurrentFPS(self)) );
}

static inline double func_brightnessreduction_SquareBrightnessReduction(StrobeAPI_t *self, double base)
{
	return lossCalculator( sqrt(base), sqrt(base * self->Helpers.effectiveFPS(self) / self->get.CurrentFPS(self)) );
}

static inline double func_brightnessreduction_CubeBrightnessReduction(StrobeAPI_t *self, double base)
{
	return lossCalculator( cbrt(base), cbrt(base * self->Helpers.effectiveFPS(self) / self->get.CurrentFPS(self)) );
}

static inline double func_brightnessreduction_OtherBrightnessReduction(StrobeAPI_t *self, double base, double(*reductionFunction)(double))
{
	return lossCalculator(reductionFunction(base), reductionFunction(base * self->Helpers.effectiveFPS(self) / self->get.CurrentFPS(self)));
}

static inline double func_experimental_Badness_Reducted(StrobeAPI_t *self, qboolean PWMInvolved)
{
	double badness;
	int diffP_NB, diffN_NB;
	diffP_NB = (self->protected->pNCounter - self->protected->pBCounter);
	diffN_NB = (self->protected->nNCounter - self->protected->nBCounter);
	double diffP = 0.0, diffN = 0.0;
	double Diff;

	if (self->protected->pCounter != 0)
		diffP = round(abs(diffP_NB) * 100 / self->protected->pCounter);

	if (self->protected->nCounter != 0)
		diffN = round(abs(diffN_NB) * 100 / self->protected->nCounter);

	if (diffP_NB < 0.0)
		diffP = -diffP;
	if (diffN_NB < 0.0)
		diffN = -diffN;

	Diff = fabs(diffP - diffN);


	if (Diff < 0.0)
		Diff = 0.0;
	else if (Diff > 200.0)
		Diff = 200.0;
	
	badness = -log((200 - Diff) / (Diff));

	if (PWMInvolved)
		return (badness * func_pwmsimulation_Period(self));
	else
		return badness;
}

static inline double func_experimental_Badness(StrobeAPI_t *self, qboolean PWMInvolved)
{
	int diffP_NB, diffN_NB;
	diffP_NB = (self->protected->pNCounter - self->protected->pBCounter); 
	diffN_NB = (self->protected->nNCounter - self->protected->nBCounter); 

	double diffP = 0.0, diffN = 0.0;
	
	if (self->protected->pCounter != 0)
		diffP = round(abs(diffP_NB) * 100 / self->protected->pCounter);

	if (self->protected->nCounter != 0)
		diffN = round(abs(diffN_NB) * 100 / self->protected->nCounter);


	double absoluteDifference = fabs(diffP - diffN);
	if (absoluteDifference > 100.0)
		absoluteDifference = 100.0;
	double badness = \
		-log(((absoluteDifference + func_helper_GeometricMean((100.0 - diffP), (100.0 - diffN))) / (absoluteDifference + func_helper_GeometricMean(diffP, diffN))));
	if (PWMInvolved)
		return (badness * func_pwmsimulation_Period(self));
	else
		return badness;
}



static inline size_t get_FrameCounter(StrobeAPI_t *self, counterType type)
{
	switch (type)
	{

	case(CT_PositiveFrame):
	{
		return self->protected->pCounter;
		break;
	}
	case(CT_PositiveNormalFrame):
	{
		return self->protected->pNCounter;
		break;
	}
	case(CT_PositiveBlackFrame):
	{
		return self->protected->pBCounter;
		break;
	}
	case(CT_NegativeFrame):
	{
		return self->protected->nCounter;
		break;
	}
	case(CT_NegativeNormalFrame):
	{
		return self->protected->nNCounter;
		break;
	}
	case(CT_NegativeBlackFrame):
	{
		return self->protected->nBCounter;
		break;
	}
	case(CT_TotalFrame):
	default:
		return self->protected->fCounter;
		break;
	}
	return self->protected->fCounter;
}

static inline double get_Deviation(StrobeAPI_t *self)
{
	return self->protected->deviation;
}

static inline double get_CooldownTimer(StrobeAPI_t *self)
{
	return self->protected->cdTimer;
}

static inline double get_currentFPS(StrobeAPI_t *self)
{
	// Copied from SCR_DrawFps
	// This way until current fps becomes global!!!

	static double	nexttime = 0, lasttime = 0;
	static double	framerate = 0;
	static unsigned int mark = 0;
	double	newtime;

	newtime = Sys_DoubleTime();
	if (newtime >= nexttime)
	{
		framerate = (self->protected->fCounter - mark) / (newtime - lasttime);
		lasttime = newtime;
		nexttime = max(nexttime + 0.5, lasttime - 0.5); // Make Update tick configurable ?
		mark = self->protected->fCounter;
	}
	
	return framerate;
}

static inline void GenerateDebugStatistics(StrobeAPI_t *self, char *src, int size)
{
	char diffBarP[128], diffBarN[128], diffBarT[128];

	int diffP_NB, diffN_NB;
	diffP_NB = (self->protected->pNCounter - self->protected->pBCounter); 
	diffN_NB = (self->protected->nNCounter - self->protected->nBCounter); 

	double diffP = 0.0, diffN = 0.0;

	if (self->protected->pCounter != 0)
		diffP = round(abs(diffP_NB) * 100 / self->protected->pCounter);

	if (self->protected->nCounter != 0)
		diffN = round(abs(diffN_NB) * 100 / self->protected->nCounter);

	self->Helpers.GenerateDiffBar(self, diffBarP, sizeof(diffBarP),0);
	self->Helpers.GenerateDiffBar(self, diffBarN, sizeof(diffBarN), 1);
	self->Helpers.GenerateDiffBar(self, diffBarT, sizeof(diffBarT),2);

	double cooldown = self->get.CooldownTimer(self);

	Q_snprintf(src,
		size,
		"%.2f FPS\n%.2f eFPS\n" \
		"Total Frame Count: %u\n" \
		"(+) Phase Frame Count: %u\n" \
		" |-> Normal Frame Count: %u\n" \
		" |-> Black Frame Count: %u\n" \
		"(-) Phase Frame Count:%u\n" \
		" |-> Normal Frame Count: %u\n" \
		" |-> Black Frame Count: %u\n" \
		".isPhaseInverted: %d\n" \
		"^5=====ANALYSIS=====\n^3" \
		"PWM Simulation:\n" \
		" |-> Frequency: %d Hz\n" \
		" |-> Duty Cycle: %.2f%%\n" \
		" |-> Current Phase Shift: +%.4f msec || -%.4f msec\n" \
		" |-> Period: %.4f msec\n" \
		"Brightness Reduction:\n" \
		" |-> [^7LINEAR^3] Actual Reduction: %3f%%\n" \
		" |-> [^7LOG^3] Realistic Reduction (400 cd/m2 base) : %.2f%%\n" \
		" |-> [^7SQUARE^3] Realistic Reduction (400 cd/m2 base) : %.2f%%\n" \
		" |-> [^7CUBE^3] Realistic Reduction (400 cd/m2 base) : %.2f%%\n" \
		"Difference (+): %s\nDifference (-): %s\nDifference (x): %s\n"  /* Diff 3 (Total): |Diff + - Diff -| . Max 200  Min 0*/ \
		"Geometric Mean: %.4f\n" \
		"G/A Difference: %.4f\n" \
		"[^7EXPERIMENTAL^3] Badness: %.4f\n" \
		"[^7EXPERIMENTAL^3] Badness x PWM Period: %.4f\n" \
		"[^7EXPERIMENTAL^3] Badness (Reducted): %.4f\n" \
		"[^7EXPERIMENTAL^3] Badness (Reducted) x PWM Period: %.4f\n" /* Badness -log((200-n)/n) */ \
		"Stability:\n" \
		" |-> Standard Deviation: %.3f\n" \
		" |-> Cooldown: %s\n" \
		"^5=====ANALYSIS=====\n^3" \
		, self->get.CurrentFPS(self) \
		, self->Helpers.effectiveFPS(self) \
		, self->get.FrameCounter(self, CT_TotalFrame) \
		, self->get.FrameCounter(self, CT_PositiveFrame), self->get.FrameCounter(self, CT_PositiveNormalFrame), self->get.FrameCounter(self, CT_PositiveBlackFrame) \
		, self->get.FrameCounter(self, CT_NegativeFrame), self->get.FrameCounter(self, CT_NegativeNormalFrame), self->get.FrameCounter(self, CT_NegativeBlackFrame) \
		, self->Helpers.isPhaseInverted(self) \
		, self->PWM.Frequency(self) \
		, self->PWM.DutyCycle() \
		, self->PWM.PositivePhaseShift(self) \
		, self->PWM.NegativePhaseShift(self) \
		, self->PWM.Period(self) \
		, self->BrightnessReductions.ActualBrightnessReduction(self)
		, self->BrightnessReductions.LogarithmicBrightnessReduction(self, 400.0)
		, self->BrightnessReductions.SquareBrightnessReduction(self, 400.0)
		, self->BrightnessReductions.CubeBrightnessReduction(self, 400.0)
		, diffBarP, diffBarN, diffBarT \
		, self->Helpers.GeometricMean(diffP, diffN) \
		, self->Helpers.ArithmeticMean(diffP, diffN) - self->Helpers.GeometricMean(diffP, diffN)
		, self->Experimentals.BADNESS(self, false), self->Experimentals.BADNESS(self, true) \
		, self->Experimentals.BADNESS_REDUCTED(self, false), self->Experimentals.BADNESS_REDUCTED(self, true)  \
		, self->get.Deviation(self)
		, (cooldown > 0.0 && self->protected->cdTriggered ? va("^1 %.2f secs\n[STROBING DISABLED] ^3", (double)r_strobe_cooldown->integer - cooldown) : "0"));

}


static inline void ProcessFrame(StrobeAPI_t *self)
{
	if (self->protected->cdTriggered != 0)
	{
		self->protected->frameInfo = f_normal | (self->protected->frameInfo & p_positive);
	}

	if (self->protected->frameInfo & f_normal) // Show normal
	{
		if (self->protected->frameInfo & p_positive)
			++self->protected->pNCounter;
		else
			++self->protected->nNCounter;

		R_Set2DMode(false);
	}
	else // Show black
	{
		if (self->protected->frameInfo & p_positive)
			++self->protected->pBCounter;
		else
			++self->protected->nBCounter;

		//GL_GenerateBlackFrame();
		self->GenerateBlackFrame();
	}

	++self->protected->fCounter;
}


void StrobeAPI_constructor(StrobeAPI_t *self) 
{	
	/*self->protected = (StrobeAPI_protected_t *)malloc(sizeof(StrobeAPI_protected_t));
	self->protected->nCounter = 0; self->protected->pBCounter = 0; self->protected->pNCounter = 0;
	self->protected->pCounter = 0; self->protected->nBCounter = 0; self->protected->nNCounter = 0;
	self->protected->fCounter = 0;
	self->protected->deviation = 0.0;
	self->protected->cdTimer = 0.0;
	self->protected->cdTriggered = false;*/
	self->protected = (StrobeAPI_protected_t *)calloc(1, sizeof(StrobeAPI_protected_t));
	self->protected->frameInfo = (p_positive | f_normal);
	self->Helpers.ArithmeticMean = func_helper_ArithmeticMean;
	self->Helpers.effectiveFPS = func_helper_effectiveFPS;
	self->Helpers.GenerateDiffBar = func_helper_GenerateDiffBar;
	self->Helpers.GeometricMean = func_helper_GeometricMean;
	self->get.Cooldown = func_helper_getCooldown;
	self->get.CurrentFPS = get_currentFPS;
	self->Helpers.isPhaseInverted = func_helper_isPhaseInverted;
	self->Helpers.isNormal = func_helper_isNormal;
	self->Helpers.isPositive = func_helper_isPositive;
	self->Helpers.StandardDeviation = func_helper_StandardDeviation;
	self->Experimentals.BADNESS = func_experimental_Badness;
	self->Experimentals.BADNESS_REDUCTED = func_experimental_Badness_Reducted;
	self->BrightnessReductions.ActualBrightnessReduction = func_brightnessreduction_ActualBrightnessReduction;
	self->BrightnessReductions.CubeBrightnessReduction = func_brightnessreduction_CubeBrightnessReduction;
	self->BrightnessReductions.LogarithmicBrightnessReduction = func_brightnessreduction_LogarithmicBrightnessReduction;
	self->BrightnessReductions.SquareBrightnessReduction = func_brightnessreduction_SquareBrightnessReduction;
	self->BrightnessReductions.OtherBrightnessReduction = func_brightnessreduction_OtherBrightnessReduction;
	self->PWM.Frequency = func_pwmsimulation_Frequency;
	self->PWM.DutyCycle = func_pwmsimulation_DutyCycle;
	self->PWM.PositivePhaseShift = func_pwmsimulation_PositivePhaseShift;
	self->PWM.NegativePhaseShift = func_pwmsimulation_NegativePhaseShift;
	self->PWM.Period = func_pwmsimulation_Period;
	self->GenerateBlackFrame = GL_GenerateBlackFrame;
	self->ProcessFrame = ProcessFrame;
	self->Helpers.GenerateDebugStatistics = GenerateDebugStatistics;
	self->get.FrameCounter = get_FrameCounter;
	self->get.Deviation = get_Deviation;
	self->get.CooldownTimer = get_CooldownTimer;
}

void StrobeAPI_destructor(StrobeAPI_t *self)
{
	if (self->protected)
	{
		free(self->protected);
		self->protected = NULL;
	}
}


/*
===============
R_InitStrobe

register strobe cvar
===============
*/
void R_InitStrobe()
{
	r_strobe = Cvar_Get("r_strobe", "0", CVAR_ARCHIVE, "black frame replacement interval");
	r_strobe_swapinterval = Cvar_Get("r_strobe_swapinterval", "0", CVAR_ARCHIVE, "swapping phase interval");
	r_strobe_debug = Cvar_Get("r_strobe_debug", "0", CVAR_ARCHIVE, "show strobe debug information");
	r_strobe_cooldown = Cvar_Get("r_strobe_cooldown", "3", CVAR_ARCHIVE, "strobe cooldown period in seconds");
}

void Strobe_Invoker(void **self, void(*constructor)(void **), void(*main)(void **), void(*destructor)(void **))
{
	if (r_strobe->integer)
	{
		if (!(*self))
			constructor(self);
		main(self);
	}
	else
	{
		if (*self)
			destructor(self);
	}
}


#endif