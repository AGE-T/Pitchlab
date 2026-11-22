import type { Metadata } from "next";
import { Geist, Geist_Mono } from "next/font/google";
import "./globals.css";
import { Toaster } from "@/components/ui/toaster";

const geistSans = Geist({
  variable: "--font-geist-sans",
  subsets: ["latin"],
});

const geistMono = Geist_Mono({
  variable: "--font-geist-mono",
  subsets: ["latin"],
});

export const metadata: Metadata = {
  title: "PitchLab Research — Round 26: Bit-Exact SIMD Read Kernels + the Original Octavox DSP Chain",
  description:
    "Round 26 leads with the bit-exact batched interpolated-read SIMD kernel round (lane-per-read AVX2 kernel + cross-voice request pool; 115/115 Release+ASan; kernel 2.2x/read; honest +1-2% production wall, Amdahl: the serial voice state machine is next), followed by the complete Round-25 map of the ORIGINAL Octavox VST3 (the complete, evidence-graded static map of the ORIGINAL Eventide Octavox VST3's internal DSP chain (target Ox.vst3, never executed) — JuceVST3Component → VoxAudioProcessor wrapper, mimo/miso/siso profile selection, the 395-module graph, the dtonicii9 engine's on-board periodicity tracker (~12 kHz decimation FIR, 243-lag windowed lag-difference, 10-state machine, record ring), the diatonic harmony mapper (5 temperament tables, 19 scale patterns, key/interval muxes), the 8-voice variable-rate delay-line reader (quartic rate law 4·P(f)·ratio·correction, 256×6 static interpolation kernel, settled/crossfade paths), per-voice HP/EQ/de-click/feedback, stereomixer3 (linear constant-sum pan law L=0.5(1−p)g), global feedback with DC blocker, wet/dry and output staging — every address and constant cited, OBS/DER/UNK classified; R25 closed the tracker FIR coefficients (3 SR classes, bit-exact halving chain) and the pan law; the R24-W Windows VST3 delivery (Harmonizer Balanced + FullRate) remains the primary package.",
  keywords: ["Octavox", "Eventide", "reverse engineering", "REA", "Ghidra", "DSP", "pitch shifting", "dtonicii9", "diatonic harmonizer", "VST3"],
  authors: [{ name: "Z.ai Team" }],
  icons: {
    icon: "https://z-cdn.chatglm.cn/z-ai/static/logo.svg",
  },
  openGraph: {
    title: "PitchLab Research — Round 26: SIMD Read Kernels + the Octavox DSP Chain",
    description: "The complete internal DSP chain of the original Eventide Octavox VST3 — static, evidence-graded map (OBS/DER/UNK), R25: FIR coefficients + pan law closed.",
    url: "https://chat.z.ai",
    siteName: "Z.ai",
    type: "website",
  },
  twitter: {
    card: "summary_large_image",
    title: "PitchLab Research — Round 26: SIMD Read Kernels + the Octavox DSP Chain",
    description: "The complete internal DSP chain of the original Eventide Octavox VST3 — static, evidence-graded map.",
  },
};

export default function RootLayout({
  children,
}: Readonly<{
  children: React.ReactNode;
}>) {
  return (
    <html lang="hu" suppressHydrationWarning>
      <body
        className={`${geistSans.variable} ${geistMono.variable} antialiased bg-background text-foreground`}
      >
        {children}
        <Toaster />
      </body>
    </html>
  );
}
