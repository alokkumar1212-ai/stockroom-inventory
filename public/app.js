const kind = document.querySelector('#kind');
if (kind) {
  const cost = document.querySelector('#cost');
  function updateCostField() {
    cost.hidden = kind.value === 'sale';
    cost.querySelector('input').disabled = cost.hidden;
  }
  kind.addEventListener('change', updateCostField);
  updateCostField();
}
document.querySelector('#print')?.addEventListener('click', () => window.print());
