'use client';
import { useDocsSearch } from 'fumadocs-core/search/client';
import { create } from '@orama/orama';
import { SearchDialog, SharedProps } from 'fumadocs-ui/components/dialog/search';

function initOrama(locale?: string) {
  return create({
    schema: { _: 'string' },
    language: locale === 'ru' ? 'russian' : 'english',
  });
}

export default function DefaultSearchDialog(props: SharedProps) {
    const client = useDocsSearch({
        type: 'static',
        initOrama
    });

    return (<SearchDialog 
        onSearchChange={client.setSearch} 
        search={client.search} 
        isLoading={client.query.isLoading}
        results={client.query.data ?? "empty"}
        {...props}
>

  </SearchDialog>);
}