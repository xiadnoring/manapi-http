import type { BaseLayoutProps } from 'fumadocs-ui/layouts/shared';
import { BookIcon } from 'lucide-react';

export const baseOptions: BaseLayoutProps = {
  nav: {
    title: (
      <span className='font-bold bg-clip-text text-transparent bg-gradient-to-r from-red-300 to-purple-400'>
        Manapi Http
      </span>
    )
  },
  githubUrl: "https://github.com/xiadnoring/manapi-http",
};
