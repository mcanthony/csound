/**
 * FLUID SYNTH OPCODES
 *
 * Adapts Fluidsynth to use global engines, soundFonts, and outputs
 *
 * Based on work by Michael Gogins and Steven Yi.  License is identical to
 * SOUNDFONTS VST License (listed below)
 *
 * Copyright (c) 2003 by Steven Yi. All rights reserved.
 *
 * [ORIGINAL INFORMATION BELOW]
 *
 * S O U N D F O N T S   V S T
 *
 * Adapts Fluidsynth to be both a VST plugin instrument
 * and a Csound plugin opcode.
 * Copyright (c) 2001-2003 by Michael Gogins. All rights reserved.
 *
 * L I C E N S E
 *
 * This software is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this software; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "csdl.h"
#include "fluidOpcodes.h"
#include "csGblMtx.h"
#include "OpcodeBase.hpp"

#include <map>
#include <vector>
#include <string>

/**
 * This may help avoid problems with the order of static initializations.
 */
static std::map<CSOUND *, std::vector<fluid_synth_t *> > &getFluidSynthsForCsoundInstances()
{
  static std::map<CSOUND *, std::vector<fluid_synth_t *> > fluidSynthsForCsoundInstances;
  return fluidSynthsForCsoundInstances;
}

/**
 * Template union for safely and efficiently
 * typecasting the value of a MYFLT variable
 * to the address of an array, and vice versa.
 */
template<typename A, typename F>
struct AddressCaster
{
  union {
    A* a;
    F f;
  };
};

/**
 * Safely and efficiently typecast an address
 * to the value of a MYFLT variable.
 */
template<typename A, typename F> void tof(A *a, F *f)
{
  AddressCaster<A, F> addressCaster;
  addressCaster.a = a;
  *f = addressCaster.f;
};

/**
 * Safely and efficiently typecast the value
 * of a MYFLT variable to an address.
 */
template<typename A, typename F> void toa(F *f, A *&a)
{
  AddressCaster<A, F> addressCaster;
  addressCaster.f = *f;
  a = addressCaster.a;
};

class FluidEngine : public OpcodeBase<FluidEngine>
{
  // Outputs.
  MYFLT *iFluidSynth;
  // Inputs.
  MYFLT *iChorusEnabled;
  MYFLT *iReverbEnabled;
  MYFLT *iChannelCount;
  MYFLT *iVoiceCount;
  // State.
  fluid_synth_t *fluidSynth;
  fluid_settings_t *fluidSettings;
  int chorusEnabled;
  int reverbEnabled;
  int channelCount;
  int voiceCount;
public:
  int init(CSOUND *csound)
  {
    fluid_synth_t *fluidSynth = 0;
    fluid_settings_t *fluidSettings = 0;
    chorusEnabled = (int) *iChorusEnabled;
    reverbEnabled = (int) *iReverbEnabled;
    channelCount = (int) *iChannelCount;
    voiceCount = (int) *iVoiceCount;
    if (channelCount <= 0) {
      channelCount = 256;
    } else if (channelCount < 16) {
      channelCount = 16;
    } else if (channelCount > 256) {
      channelCount = 256;
    }
    if (voiceCount <= 0) {
      voiceCount = 4096;
    } else if (voiceCount < 16) {
      voiceCount = 16;
    } else if (voiceCount > 4096) {
      voiceCount = 4096;
    }
    csound_global_mutex_lock();
    fluidSettings = new_fluid_settings();
    if (fluidSettings != NULL) {
      fluid_settings_setnum(fluidSettings,
                            (char *)"synth.sample-rate", (double) csound->esr);
      fluid_settings_setint(fluidSettings,
                            (char *)"synth.midi-channels", channelCount);
      fluid_settings_setint(fluidSettings,
                            (char *)"synth.polyphony", voiceCount);
      fluidSynth = new_fluid_synth(fluidSettings);
    }
    csound_global_mutex_unlock();
    if (!fluidSynth) {
      if (fluidSettings)
        delete_fluid_settings(fluidSettings);
      return csound->InitError(csound, Str("error allocating fluid engine\n"));
    }
    csound_global_mutex_lock();
    fluid_synth_set_chorus_on(fluidSynth, chorusEnabled);
    fluid_synth_set_reverb_on(fluidSynth, reverbEnabled);
    csound_global_mutex_unlock();
    log(csound, "Created fluidEngine 0x%p with sampling rate = %f, "
        "chorus %s, reverb %s, channels %d, voices %d.\n",
        fluidSynth, (double) csound->esr,
        chorusEnabled ? "on" : "off",
        reverbEnabled ? "on" : "off",
        channelCount,
        voiceCount);
    tof(fluidSynth, iFluidSynth);
    getFluidSynthsForCsoundInstances()[csound].push_back(fluidSynth);
    return OK;
  }
};

class FluidLoad : public OpcodeBase<FluidLoad>
{
  // Outputs.
  MYFLT *iInstrumentNumber;
  // Inputs.
  MYFLT *iFilename;
  MYFLT *iFluidSynth;
  MYFLT *iListPresets;
  // State.
  char *filename;
  char *filepath;
  fluid_synth_t *fluidSynth;
  int soundFontId;
  int listPresets;
public:
  int init(CSOUND *csound)
  {
    soundFontId = -1;
    toa(iFluidSynth, fluidSynth);
    listPresets = (int) *iListPresets;
    filename = csound->strarg2name(csound,
                                   (char*) NULL,
                                   iFilename,
                                   (char *)"fluid.sf2.",
                                   (int) csound->GetInputArgSMask(this));
    filepath = csound->FindInputFile(csound, filename, "SFDIR;SSDIR");
    if (filepath && fluid_is_soundfont(filepath)) {
      log(csound, "Loading SoundFont : %s.\n", filepath);
      soundFontId = fluid_synth_sfload(fluidSynth,
                                       filepath,
                                       0);
      log(csound, "fluidSynth: 0x%p  soundFontId: %d.\n", fluidSynth, soundFontId);
    }
    *iInstrumentNumber = (MYFLT) soundFontId;
    if (soundFontId < 0) {
      csound->InitError(csound, Str("fluid: unable to load %s"), filename);
    }
    csound->NotifyFileOpened(csound, filepath, CSFTYPE_SOUNDFONT, 0, 0);
    if (soundFontId < 0) {
      return NOTOK;
    }
    if (listPresets) {
      fluid_sfont_t *fluidSoundfont = fluid_synth_get_sfont_by_id(fluidSynth, soundFontId);
      fluid_preset_t fluidPreset;
      fluidSoundfont->iteration_start(fluidSoundfont);
      if (csound->oparms->msglevel & 0x7)
        while (fluidSoundfont->iteration_next(fluidSoundfont, &fluidPreset)) {
          log(csound,
              "SoundFont: %3d  Bank: %3d  Preset: %3d  %s\n",
              soundFontId,
              fluidPreset.get_banknum(&fluidPreset),
              fluidPreset.get_num(&fluidPreset),
              fluidPreset.get_name(&fluidPreset));
        }
    }
    return OK;
  }
};

class FluidProgramSelect : public OpcodeBase<FluidProgramSelect>
{
  // Inputs.
  MYFLT *iFluidSynth;
  MYFLT *iChannelNumber;
  MYFLT *iInstrumentNumber;
  MYFLT *iBankNumber;
  MYFLT *iPresetNumber;
  // State.
  fluid_synth_t *fluidSynth;
  int channel;
  unsigned int instrument;
  unsigned int bank;
  unsigned int preset;
public:
  int init(CSOUND *csound)
  {
    toa(iFluidSynth, fluidSynth);
    channel = (int) *iChannelNumber;
    instrument = (unsigned int) *iInstrumentNumber;
    bank = (unsigned int) *iBankNumber;
    preset = (unsigned int) *iPresetNumber;
    fluid_synth_program_select(fluidSynth,
                               channel,
                               instrument,
                               bank,
                               preset);

    return OK;
  }
};

class FluidCCI : public OpcodeBase<FluidCCI>
{
  // Inputs.
  MYFLT *iFluidSynth;
  MYFLT *iChannelNumber;
  MYFLT *iControllerNumber;
  MYFLT *kVal;
  // State.
  fluid_synth_t *fluidSynth;
  int channel;
  int controller;
  int value;
public:
  int init(CSOUND *csound)
  {
    toa(iFluidSynth, fluidSynth);
    channel = (int) *iChannelNumber;
    controller = (int) *iControllerNumber;
    value = (int) *kVal;
    fluid_synth_cc(fluidSynth, channel, controller, value);
    return OK;
  }
};

class FluidCCK : public OpcodeBase<FluidCCK>
{
  // Inputs.
  MYFLT *iFluidSynth;
  MYFLT *iChannelNumber;
  MYFLT *iControllerNumber;
  MYFLT *kVal;
  // State.
  fluid_synth_t *fluidSynth;
  int channel;
  int controller;
  int value;
  int priorValue;
public:
  int init(CSOUND *csound)
  {
    toa(iFluidSynth, fluidSynth);
    priorValue = -1;
    return OK;
  }
  int kontrol(CSOUND *csound)
  {
    value = (int) *kVal;
    if (value != priorValue) {
      channel = (int) *iChannelNumber;
      controller = (int) *iControllerNumber;
      fluid_synth_cc(fluidSynth, channel, controller, value);
    }
    return OK;
  }
};

class FluidNote : public OpcodeNoteoffBase<FluidNote>
{
  // Inputs.
  MYFLT *iFluidSynth;
  MYFLT *iChannelNumber;
  MYFLT *iMidiKeyNumber;
  MYFLT *iVelocity;
  // State.
  fluid_synth_t *fluidSynth;
  int channel;
  int key;
  int velocity;
public:
  int init(CSOUND *csound)
  {
    toa(iFluidSynth, fluidSynth);
    channel = (int) *iChannelNumber;
    key = (int) *iMidiKeyNumber;
    velocity = (int) *iVelocity;
    fluid_synth_noteon(fluidSynth, channel, key, velocity);
    return OK;
  }
  int noteoff(CSOUND *csound)
  {
    fluid_synth_noteoff(fluidSynth, channel, key);
    return OK;
  }
};

class FluidOut : public OpcodeBase<FluidOut>
{
  // Outputs.
  MYFLT *aLeftOut;
  MYFLT *aRightOut;
  // Inputs.
  MYFLT *iFluidSynth;
  // State.
  fluid_synth_t *fluidSynth;
  float leftSample;
  float rightSample;
  int frame;
  int ksmps;
public:
  int init(CSOUND *csound)
  {
    toa(iFluidSynth, fluidSynth);
    ksmps = csound->GetKsmps(csound);
    return OK;
  }
  int audio(CSOUND *csound)
  {
    for (frame = 0; frame < ksmps; frame++) {
      leftSample = 0.0f;
      rightSample = 0.0f;
      fluid_synth_write_float(fluidSynth, 1, &leftSample, 0, 1, &rightSample, 0, 1);
      aLeftOut[frame] = leftSample /* * csound->e0dbfs */;
      aRightOut[frame] = rightSample /* * csound->e0dbfs */;
    }
    return OK;
  }
};

class FluidAllOut : public OpcodeBase<FluidAllOut>
{
  // Outputs.
  MYFLT *aLeftOut;
  MYFLT *aRightOut;
  // State.
  float leftSample;
  float rightSample;
  int frame;
  int ksmps;
public:
  int init(CSOUND *csound)
  {
    ksmps = csound->GetKsmps(csound);
    return OK;
  }
  int audio(CSOUND *csound)
  {
    std::vector<fluid_synth_t *> &fluidSynths = getFluidSynthsForCsoundInstances()[csound];
    for (frame = 0; frame < ksmps; frame++) {
      aLeftOut[frame] = FL(0.0);
      aRightOut[frame] = FL(0.0);
      for (size_t i = 0, n = fluidSynths.size(); i < n; i++) {
        fluid_synth_t *fluidSynth = fluidSynths[i];
        leftSample = 0.0f;
        rightSample = 0.0f;
        fluid_synth_write_float(fluidSynth, 1, &leftSample, 0, 1, &rightSample, 0, 1);
        aLeftOut[i] += (MYFLT) leftSample /* * csound->e0dbfs */;
        aRightOut[i] += (MYFLT) rightSample /* * csound->e0dbfs */;
      }
    }
    return OK;
  }
};

class FluidControl : public OpcodeBase<FluidControl>
{
  // Inputs.
  MYFLT *iFluidSynth;
  MYFLT *kMidiStatus;
  MYFLT *kMidiChannel;
  MYFLT *kMidiData1;
  MYFLT *kMidiData2;
  // State.
  fluid_synth_t *fluidSynth;
  int midiStatus;
  int midiChannel;
  int midiData1;
  int midiData2;
  int priorMidiStatus;
  int priorMidiChannel;
  int priorMidiData1;
  int priorMidiData2;
  int printMsgs;
public:
  int init(CSOUND *csound)
  {
    toa(iFluidSynth, fluidSynth);
    priorMidiStatus = -1;
    priorMidiChannel = -1;
    priorMidiData1 = -1;
    priorMidiData2 = -1;
    printMsgs = ((csound->oparms->msglevel & 7) == 7 ? 1 : 0);
    return OK;
  }
  int kontrol(CSOUND *csound)
  {
    midiStatus = 0xF0 & (int) *(kMidiStatus);
    midiChannel = (int) *kMidiChannel;
    midiData1 = (int) *kMidiData1;
    midiData2 = (int) *kMidiData2;
    int result =  -1;
    if (midiData2 != priorMidiData2 ||
        midiData1 != priorMidiData1 ||
        midiChannel != priorMidiChannel ||
        midiStatus != priorMidiStatus) {
      switch (midiStatus) {
      case (int) 0x80:
      noteOff:
        result = fluid_synth_noteoff(fluidSynth, midiChannel, midiData1);
      if (printMsgs)
        csound->Message(csound,
                        "result: %d \n Note off: c:%3d k:%3d\n",
                        result,
                        midiChannel,
                        midiData1);
      break;
      case (int) 0x90:
        if (!midiData2)
          goto noteOff;
        result = fluid_synth_noteon(fluidSynth, midiChannel, midiData1, midiData2);
        if (printMsgs)
          log(csound, "result: %d \nNote on: c:%3d k:%3d v:%3d\n",result,
              midiChannel, midiData1, midiData2);
        break;
      case (int) 0xA0:
        if (printMsgs)
          log(csound, "Key pressure (not handled): "
              "c:%3d k:%3d v:%3d\n",
              midiChannel, midiData1, midiData2);
        break;
      case (int) 0xB0:
        result = fluid_synth_cc(fluidSynth, midiChannel, midiData1, midiData2);
        if (printMsgs)
          log(csound, "Result: %d Control change: c:%3d c:%3d v:%3d\n",result,
              midiChannel, midiData1, midiData2);
        break;
      case (int) 0xC0:
        result = fluid_synth_program_change(fluidSynth, midiChannel, midiData1);
        if (printMsgs)
          log(csound, "Result: %d Program change: c:%3d p:%3d\n",result,
              midiChannel, midiData1);
        break;
      case (int) 0xD0:
        if (printMsgs)
          log(csound, "After touch (not handled): c:%3d v:%3d\n",
              midiChannel, midiData1);
        break;
      case (int) 0xE0:
        {
          int pbVal = midiData1 + (midiData2 << 7);
          fluid_synth_pitch_bend(fluidSynth, midiChannel, pbVal);
          if (printMsgs)
            log(csound, "Result: %d, Pitch bend:     c:%d b:%d\n", result,
                midiChannel, pbVal);
        }
        break;
      case (int) 0xF0:
        if (printMsgs)
          log(csound, "System exclusive (not handled): "
              "c:%3d v1:%3d v2:%3d\n",
              midiChannel, midiData1, midiData2);
        break;
      }
      priorMidiStatus = midiStatus;
      priorMidiChannel = midiChannel;
      priorMidiData1 = midiData1;
      priorMidiData2 = midiData2;
    }
    return OK;
  }
};

class FluidSetInterpMethod : public OpcodeBase<FluidSetInterpMethod>
{
  // Inputs.
  MYFLT *iFluidSynth;
  MYFLT *iChannelNumber;
  MYFLT *iInterpMethod;
  // State.
  fluid_synth_t *fluidSynth;
  int channel;
  int interpolationMethod;
public:
  int init(CSOUND *csound)
  {
    toa(iFluidSynth, fluidSynth);
    channel = (int) *iChannelNumber;
    interpolationMethod = (int) *iInterpMethod;
    if(interpolationMethod != 0 && interpolationMethod != 1 && interpolationMethod != 4 &&
       interpolationMethod != 7)
      {
        csound->InitError(csound,
                          Str("Illegal Interpolation Method: Must be "
                              "either 0, 1, 4, or 7.\n"));
        return NOTOK;
      }
    fluid_synth_set_interp_method(fluidSynth, channel, interpolationMethod);
    return OK;
  }
};

static OENTRY localops[] = {
  {
    (char *)"fluidEngine",
    sizeof(FluidEngine),
    1,
    (char *)"i",
    (char *)"ppoo",
    (SUBR) &FluidEngine::init_,
    (SUBR) 0,
    (SUBR) 0
  },
  {
    (char *)"fluidLoad",
    sizeof(FluidLoad),
    1,
    (char *)"i",
    (char *)"Tio",
    (SUBR) &FluidLoad::init_,
    (SUBR) 0,
    (SUBR) 0
  },
  {
    (char *)"fluidProgramSelect",
    sizeof(FluidProgramSelect),
    1,
    (char *)"",
    (char *)"iiiii",
    (SUBR) &FluidProgramSelect::init_,
    (SUBR) 0,
    (SUBR) 0
  },
  {
    (char *)"fluidCCi",
    sizeof(FluidCCI),
    1,
    (char *)"",
    (char *)"iiii",
    (SUBR) &FluidCCI::init_,
    (SUBR) 0,
    (SUBR) 0
  },
  {
    (char *)"fluidCCk",
    sizeof(FluidCCK),
    3,
    (char *)"",
    (char *)"ikkk",
    (SUBR) &FluidCCK::init_,
    (SUBR) &FluidCCK::kontrol_,
    (SUBR) 0
  },
  {
    (char *)"fluidNote",
    sizeof(FluidNote),
    3,
    (char *)"",
    (char *)"iiii",
    (SUBR) &FluidNote::init_,
    (SUBR) &FluidNote::kontrol_,
    (SUBR) 0
  },
  {
    (char *)"fluidOut",
    sizeof(FluidOut),
    5,
    (char *)"aa",
    (char *)"i",
    (SUBR) &FluidOut::init_,
    (SUBR) 0,
    (SUBR) &FluidOut::audio_
  },
  {
    (char *)"fluidAllOut",
    sizeof(FluidAllOut),
    5,
    (char *)"aa",
    (char *)"i",
    (SUBR) &FluidAllOut::init_,
    (SUBR) 0,
    (SUBR) &FluidAllOut::audio_
  },
  {
    (char *)"fluidControl",
    sizeof(FluidControl),
    3,
    (char *)"",
    (char *)"ikkkk",
    (SUBR) FluidControl::init_,
    (SUBR) FluidControl::kontrol_,
    (SUBR) 0
  },
  {
    (char *)"fluidSetInterpMethod",
    sizeof(FluidSetInterpMethod),
    1,
    (char *)"",
    (char *)"iii",
    (SUBR) &FluidSetInterpMethod::init_,
    (SUBR) 0,
    (SUBR) 0
  },
  {
    0,
    0,
    0,
    0,
    0,
    (SUBR) 0,
    (SUBR) 0,
    (SUBR) 0
  }
};

PUBLIC int csoundModuleCreate(CSOUND *csound)
{
  (void) csound;
  return 0;
}

PUBLIC int csoundModuleInit(CSOUND *csound)
{
  OENTRY  *ep;
  int     err = 0;

  for (ep = (OENTRY *) &(localops[0]);
       ep->opname != NULL;
       ep++) {
    err |= csound->AppendOpcode(csound,
                                ep->opname,
                                ep->dsblksiz,
                                ep->thread,
                                ep->outypes,
                                ep->intypes,
                                (int (*)(CSOUND *, void *)) ep->iopadr,
                                (int (*)(CSOUND *, void *)) ep->kopadr,
                                (int (*)(CSOUND *, void *)) ep->aopadr);
  }
  return err;
}

/**
 * Called by Csound to de-initialize the opcode
 * just before destroying it.
 */

PUBLIC int csoundModuleDestroy(CSOUND *csound)
{
  for (std::map<CSOUND *, std::vector<fluid_synth_t *> >::iterator it = getFluidSynthsForCsoundInstances().begin();
       it != getFluidSynthsForCsoundInstances().end();
       ++it) {
    std::vector<fluid_synth_t *> &fluidSynths = it->second;
    for (size_t i = 0, n = fluidSynths.size(); i < n; i++) {
      fluid_synth_t *fluidSynth = fluidSynths[i];
      fluid_settings_t *fluidSettings = fluid_synth_get_settings(fluidSynth);
      delete_fluid_synth(fluidSynth);
      delete_fluid_settings(fluidSettings);
    }
  }
  return 0;
}

PUBLIC int csoundModuleInfo(void)
{
  return ((CS_APIVERSION << 16) + (CS_APISUBVER << 8) + (int) sizeof(MYFLT));
}
