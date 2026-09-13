# Changelog

Todas as mudanças relevantes do Vitrine são registradas neste arquivo.

## [1.0.3] - 2026-09-13

### Corrigido

- Mantida a mesma linha da grade ao alternar da navegação por toque para o controle.
- Mantida a posição visível ao alternar do controle para a navegação por toque.
- Preservada a coluna selecionada quando possível, inclusive nos layouts clássico e de capas.

### Adicionado

- Limite global de 100 MiB para o cache persistente de capas, screenshots, detalhes e páginas do catálogo.
- Remoção automática dos arquivos menos usados quando o cache ultrapassa o limite, reduzindo o consumo para 80 MiB.
- Aplicação da política de limite também ao iniciar o aplicativo, incluindo caches criados por versões anteriores.
- Testes automatizados para a sincronização entre métodos de entrada e para a política de remoção do cache.

### Alterado

- A tela Sobre agora apresenta o consumo atual do cache junto ao limite configurado.
- Favoritos e itens de Minha lista continuam preservados fora do cache e não participam da remoção automática.
