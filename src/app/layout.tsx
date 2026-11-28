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
  title: "PitchLab Research — Round 34: The Chain-End Stereo Width Stage (energy-preserving)",
  description:
    "Round 34 (the chain-end stereo width stage, on the r31-telemetry-demo branch): the user-requested feature — an adjustable stereo image width at the very END of the DSP chain, collapsible all the way to mono, WITH the sound's energy maintained (research first, then implement). One new parameter \u201cStereo Width\u201d (id 38, [0,1], default 1.0 = BIT-NEUTRAL — the r15acc determinism hash 0x35fe0d51a4372ac8 identical to the R31/R32/R33 records; 0.0 = bitwise mono collapse: L'==R'). The mid-only-gain law k(w,rho) = sqrt((2 - w^2*(1-rho))/(1+rho)) with the correlation measured feed-forward on the stage input (a 200 ms one-pole; no transcendental in the block path — the active stage is IEEE-exact deterministic): dual-mono content collapses at exact unity (the property that rejects the static equal-power law), uncorrelated at +3 dB, out-of-phase honestly clamped at +6 dB. Slew-limited matrix (10 ms, click-free), bypass gating, state blob v3 (v1/v2 sessions restore bit-neutral), the R33 editor's new WIDTH slider (golden re-pin 0x929df78511dc9220). The r34_energy_uncorrelated gate at w=0.5 caught the research doc's first derivation error (-0.343 dB, the wrong law's prediction to the digit) — corrected and documented. Verification: core 126/126 Release AND ASan/UBSan/LSan (115 pre-R34 cases unchanged + 11 new r34 cases), wrapper 22/22 (state v3 + v2 back-compat), ui 11/11 (hit-map 39/39, X11 pixel identity), module integration 18/18 (stereo_width_module: bitwise mono, +0.013 dB energy, bypass gate bit-exact on the shipped module), validator 47/47, Windows cross-build machine-code record identical to R32-W/R33-W. Delivery: PitchLab_R34_Stereo_Width_VST3.zip (Linux + Windows bundles + PDB + README + reports + logs + the real-render screenshots)",
  keywords: ["Octavox", "Eventide", "reverse engineering", "REA", "Ghidra", "DSP", "pitch shifting", "minimum pitch"],
  authors: [{ name: "Z.ai Team" }],
  icons: {
    icon: "https://z-cdn.chatglm.cn/z-ai/static/logo.svg",
  },
  openGraph: {
    title: "Z.ai Code Scaffold",
    description: "AI-powered development with modern React stack",
    url: "https://chat.z.ai",
    siteName: "Z.ai",
    type: "website",
  },
  twitter: {
    card: "summary_large_image",
    title: "Z.ai Code Scaffold",
    description: "AI-powered development with modern React stack",
  },
};

export default function RootLayout({
  children,
}: Readonly<{
  children: React.ReactNode;
}>) {
  return (
    <html lang="en" suppressHydrationWarning>
      <body
        className={`${geistSans.variable} ${geistMono.variable} antialiased bg-background text-foreground`}
      >
        {children}
        <Toaster />
      </body>
    </html>
  );
}
