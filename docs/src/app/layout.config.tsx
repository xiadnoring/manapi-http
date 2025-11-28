import type { BaseLayoutProps } from 'fumadocs-ui/layouts/shared';
import { BookIcon } from 'lucide-react';

export const baseOptions: BaseLayoutProps = {
  nav: {
    title: (
      <span className='text-red-300'>
        Manapi Http
      </span>
    )
  },
  githubUrl: "https://github.com/xiadnoring/manapi-http"
};
