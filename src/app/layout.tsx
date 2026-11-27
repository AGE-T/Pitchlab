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
  title: "PitchLab Research — Round 32: Windows x64 Telemetry Demo VST3",
  description:
    "Round 32 (Windows x64 build of the telemetry demo, on the r31-telemetry-demo branch): the R31 demo — m02-style pitch telemetry (ids 7..13) through the IPitchTelemetry vendor interface, per-voice block-rate poller with atomic dirty-bit fanout, latency-truth notification — ported to Windows x64 with the restored R24-W cross method (Zig 0.13.0, installed outside the repo; the recorded toolchain deviation behind the accepted R21/R22/R24-W deliveries). Two Windows-portability defects closed: (a) the lost R24-W corrective delta — zig's x86_64-windows-gnu provides no compiler-rt cpu_model, so __builtin_cpu_supports fails to link; the explicit CPUID+XGETBV detection (simd_cpuid_explicit.h, both features gated on OSXSAVE + XCR0 YMM state) is ported verbatim from the R24-W delivery package together with its on-hardware cross-check (explicit vs builtin: AGREE); (b) the stale OCTAVOX_X86_MSVC capability gates that would have dead-stripped the AVX2/FMA kernel objects (final DLL ymm=0, silently scalar-only) — fixed to the OCTAVOX_X86_EXPLICIT form; the shipped module now carries ymm=275 (207+68, every 256-bit instruction from the two ISA units), vfmadd=26, cpuid=3 + xgetbv=1. Linux-neutral proof: the fresh twin re-ran every gate (core 115/115 Release + ASan/UBSan with the determinism hash identical to the R31 record, wrapper 21/21, module integration 16/16 incl. all four telemetry cases, Steinberg validator 47/47). Delivery: PitchLab_R32_Telemetry_Demo_Windows_x64.zip (PE32+ x64 bundle + PDB + README + report + verification logs).",
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
