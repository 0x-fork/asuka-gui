// asuka/transcribe on the web: a browser has no recognizer of files on
// the device (the Web Speech API's sends the microphone to a server):
// check says so, and file fails with notAvailable rather than sending the
// sound anywhere.
import { TranscribeError } from "./transcribe.asuka.js";

export function makeRecognizer() {
  return {
    async check(signal, language) {
      return { onDevice: false, language: language || navigator.language || "" };
    },
    async file() {
      throw new TranscribeError("notAvailable");
    },
  };
}
