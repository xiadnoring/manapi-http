import './global.css';
import { RootProvider } from 'fumadocs-ui/provider';
import { Inter } from 'next/font/google';
import type { ReactNode } from 'react';
import { useDocsSearch } from 'fumadocs-core/search/client';
import { create } from '@orama/orama';
import SearchDialog from '@/components/search';

const inter = Inter({
  subsets: ['latin'],
});

export default function Layout({ children }: { children: ReactNode }) {
  return (
    <html lang="en" className={inter.className} suppressHydrationWarning>
      <link rel="icon" href="/favicon.ico" sizes="any" />
      <body className="flex flex-col min-h-screen">
        <RootProvider search={{SearchDialog}}>{children}</RootProvider>
      </body>
    </html>
  );
}
