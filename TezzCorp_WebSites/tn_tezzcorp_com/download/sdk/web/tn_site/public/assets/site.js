(() => {
  const items = Array.from(document.querySelectorAll(".reveal"));
  items.forEach((item, idx) => {
    const delay = 80 * idx;
    window.setTimeout(() => {
      item.classList.add("show");
    }, delay);
  });
})();
